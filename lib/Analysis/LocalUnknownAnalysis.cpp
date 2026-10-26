//===- LocalUnknownAnalysis.cpp - X/Z inside one gated activation ---------===//

#include "obelisk/Analysis/LocalUnknownAnalysis.h"
#include "obelisk/Analysis/KnownVersioningAnalysis.h"
#include "obelisk/Analysis/LocalStorageAnalysis.h"
#include "obelisk/Analysis/RangeSet.h"
#include "obelisk/Analysis/ScopedIntegerRangeAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Analysis/UnknownBitGeometry.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include "mlir/Analysis/CallGraph.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/RegionGraphTraits.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"

#include <algorithm>
#include <memory>

using namespace mlir;
namespace obelisk::analysis {
namespace {

using ReadKind = LocalUnknownAnalysis::ReadKind;

/// A segment's entry check tests private slots up to this width.
constexpr uint64_t maximumCheckedSlotBits = 256;

//===----------------------------------------------------------------------===//
// May-X/Z bits with and without the entry check
//===----------------------------------------------------------------------===//

/// Bottom, the bits of a value that may hold X/Z, or every bit. Known means
/// no bit may.
class MayUnknownBits {
public:
  MayUnknownBits() = default;
  static MayUnknownBits of(APInt mask) {
    MayUnknownBits bits;
    bits.state = Mask;
    bits.mask = std::move(mask);
    return bits;
  }
  static MayUnknownBits all() {
    MayUnknownBits bits;
    bits.state = All;
    return bits;
  }
  bool isBottom() const { return state == Bottom; }
  bool isKnown() const { return state == Mask && mask.isZero(); }
  /// The mask at `width` bits; every bit when the fact is not bit-precise.
  APInt get(unsigned width) const {
    if (state == Mask && mask.getBitWidth() == width)
      return mask;
    return APInt::getAllOnes(width);
  }
  static MayUnknownBits join(const MayUnknownBits &lhs,
                             const MayUnknownBits &rhs) {
    if (lhs.state == Bottom)
      return rhs;
    if (rhs.state == Bottom)
      return lhs;
    if (lhs.state == All || rhs.state == All ||
        lhs.mask.getBitWidth() != rhs.mask.getBitWidth())
      return all();
    return of(lhs.mask | rhs.mask);
  }
  bool operator==(const MayUnknownBits &other) const {
    return state == other.state && (state != Mask || mask == other.mask);
  }
  void print(raw_ostream &os) const {
    if (state == Bottom)
      os << "bottom";
    else if (state == All)
      os << "all";
    else
      os << "mask " << mask;
  }

private:
  enum State : uint8_t { Bottom, Mask, All } state = Bottom;
  APInt mask;
};

/// Facts in the gated copy.
class GatedLattice final : public dataflow::Lattice<MayUnknownBits> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(GatedLattice)
  using Lattice::Lattice;
};

/// Facts in the original body, where every canonical read is four-state.
class BaselineLattice final : public dataflow::Lattice<MayUnknownBits> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(BaselineLattice)
  using Lattice::Lattice;
};

/// The may-X/Z bits of the one result of `op` from those of its operands,
/// under the X/Z rules of IEEE 1800-2023 11.4: moves (selections,
/// concatenations, extensions, aggregates and selects with a known
/// condition) carry their bits, bitwise operators work per bit, and an X/Z
/// operand bit makes arithmetic, relations, reductions and logical operators
/// X. `input(index)` is the operand's mask, or none while it is unsolved,
/// which makes the result unsolved as well. Loads, net reads and calls are
/// not transferred here.
std::optional<APInt>
transferMayUnknown(Operation *op, unsigned width,
                   llvm::function_ref<std::optional<APInt>(unsigned)> input,
                   llvm::function_ref<bool(Operation *)> inRange) {
  using namespace bits;
  const APInt all = APInt::getAllOnes(width), none = APInt(width, 0);
  auto allIf = [&](bool unknown) { return unknown ? all : none; };
  Value result = op->getResult(0);
  bool array = isElementwise(result.getType());
  auto inputArray = [&](unsigned index) {
    return isElementwise(op->getOperand(index).getType());
  };
  auto logicOperands = [&] {
    SmallVector<unsigned, 4> indices;
    for (unsigned index = 0; index != op->getNumOperands(); ++index)
      if (hasUnknownPlane(op->getOperand(index).getType()))
        indices.push_back(index);
    return indices;
  };
  /// Whether any of `indices` may hold X/Z.
  auto anyUnknown = [&](ArrayRef<unsigned> indices) -> std::optional<bool> {
    bool any = false;
    for (unsigned index : indices) {
      if (!hasUnknownPlane(op->getOperand(index).getType()))
        continue;
      std::optional<APInt> bits = input(index);
      if (!bits)
        return std::nullopt;
      any |= !bits->isZero();
    }
    return any;
  };
  auto fromAny = [&](ArrayRef<unsigned> indices) -> std::optional<APInt> {
    std::optional<bool> any = anyUnknown(indices);
    if (!any)
      return std::nullopt;
    return allIf(*any);
  };
  /// The union of `indices`' masks, each `width` bits wide.
  auto unite = [&](ArrayRef<unsigned> indices) -> std::optional<APInt> {
    APInt mask = none;
    for (unsigned index : indices) {
      std::optional<APInt> bits = input(index);
      if (!bits)
        return std::nullopt;
      if (bits->getBitWidth() != width)
        return all;
      mask |= *bits;
    }
    return mask;
  };
  /// `bits` placed at every element of packed array `type`.
  auto everyElement = [&](Type type,
                          const APInt &bits) -> std::optional<APInt> {
    APInt mask = none;
    for (unsigned index = 0, count = sim::getAggregateNumElements(type);
         index != count; ++index) {
      auto place = subelementPlacement(type, index, width);
      if (!place || place->second != bits.getBitWidth())
        return std::nullopt;
      mask.insertBits(bits, place->first);
    }
    return mask;
  };
  // Known operands give a known result (11.4) except at the X/Z generators
  // below: X/Z constants and defaults, invalid selections, zero divisors.
  if (!isa<sim::SimLogicConstantOp, sim::SimAggregateDefaultOp,
           sim::SimLogicDynExtractOp, sim::SimArrayDynExtractOp,
           sim::SimLogicBinaryOp>(op)) {
    std::optional<bool> any = anyUnknown(logicOperands());
    if (!any)
      return std::nullopt;
    if (!*any)
      return none;
  }
  return llvm::TypeSwitch<Operation *, std::optional<APInt>>(op)
      .Case<sim::SimLogicConstantOp>(
          [&](auto constant) -> std::optional<APInt> {
            return constant.getUnknown().zextOrTrunc(width);
          })
      .Case<sim::SimLogicFromBitsOp>(
          [&](auto) -> std::optional<APInt> { return none; })
      .Case<sim::SimAggregateDefaultOp>([&](auto) -> std::optional<APInt> {
        return defaultUnknown(result.getType(), width);
      })
      .Case<sim::SimAggregateConstructOp>([&](auto) -> std::optional<APInt> {
        if (array)
          return unite(llvm::to_vector(llvm::seq(op->getNumOperands())));
        APInt mask = none;
        for (unsigned index = 0; index != op->getNumOperands(); ++index) {
          std::optional<APInt> bits = input(index);
          if (!bits)
            return std::nullopt;
          auto place = subelementPlacement(result.getType(), index, width);
          if (!place || bits->getBitWidth() != place->second)
            return all;
          mask.insertBits(*bits, place->first);
        }
        return mask;
      })
      .Case<sim::SimAggregateSplatOp>([&](auto) -> std::optional<APInt> {
        if (array)
          return unite({0});
        std::optional<APInt> bits = input(0);
        if (!bits)
          return std::nullopt;
        std::optional<APInt> mask = everyElement(result.getType(), *bits);
        return mask ? *mask : all;
      })
      .Case<sim::SimAggregateExtractOp>(
          [&](auto extract) -> std::optional<APInt> {
            if (inputArray(0))
              return unite({0});
            std::optional<APInt> bits = input(0);
            if (!bits)
              return std::nullopt;
            auto place =
                subelementPlacement(extract.getInput().getType(),
                                    extract.getIndex(), bits->getBitWidth());
            if (!place || place->second != width)
              return all;
            return bits->extractBits(width, place->first);
          })
      .Case<sim::SimAggregateInsertOp>(
          [&](auto insert) -> std::optional<APInt> {
            if (array)
              return unite({0, 1});
            std::optional<APInt> base = input(0), replacement = input(1);
            if (!base || !replacement)
              return std::nullopt;
            auto place =
                subelementPlacement(result.getType(), insert.getIndex(), width);
            if (base->getBitWidth() != width || !place ||
                replacement->getBitWidth() != place->second)
              return all;
            base->insertBits(*replacement, place->first);
            return *base;
          })
      // An invalid or X/Z index writes nothing (7.4.5, 11.5.1); a valid one
      // may replace any element.
      .Case<sim::SimArrayDynInsertOp>([&](auto) -> std::optional<APInt> {
        if (array)
          return unite({0, 1});
        std::optional<APInt> base = input(0), replacement = input(1);
        if (!base || !replacement)
          return std::nullopt;
        if (base->getBitWidth() != width)
          return all;
        if (replacement->isZero())
          return *base;
        std::optional<APInt> spread =
            everyElement(result.getType(), *replacement);
        return spread ? *base | *spread : all;
      })
      // Any element, or for an X/Z or invalid index the element's default
      // (7.4.6, 11.5.1).
      .Case<sim::SimArrayDynExtractOp>([&](auto extract)
                                           -> std::optional<APInt> {
        std::optional<APInt> bits = input(0), index = input(1);
        if (!bits || !index)
          return std::nullopt;
        if (!index->isZero())
          return all;
        APInt mask =
            inRange(op) ? none : defaultUnknown(result.getType(), width);
        Type source = extract.getInput().getType();
        if (inputArray(0))
          return bits->getBitWidth() == width ? mask | *bits : all;
        for (unsigned index = 0, count = sim::getAggregateNumElements(source);
             index != count; ++index) {
          auto place = subelementPlacement(source, index, bits->getBitWidth());
          if (!place || place->second != width)
            return all;
          mask |= bits->extractBits(width, place->first);
        }
        return mask;
      })
      // An X/Z or invalid index makes every bit X (11.5.1).
      .Case<sim::SimLogicDynExtractOp>([&](auto) -> std::optional<APInt> {
        std::optional<APInt> bits = input(0), index = input(1);
        if (!bits || !index)
          return std::nullopt;
        return allIf(!index->isZero() || !inRange(op) || !bits->isZero());
      })
      .Case<sim::SimLogicDynInsertOp>([&](auto) -> std::optional<APInt> {
        std::optional<APInt> base = input(0), replacement = input(1);
        if (!base || !replacement)
          return std::nullopt;
        if (base->getBitWidth() != width)
          return all;
        return *base | allIf(!replacement->isZero());
      })
      // An X/Z condition keeps a bit only where both operands hold the same
      // known value (11.4.11, Table 11-20).
      .Case<sim::SimLogicMuxOp>([&](auto) -> std::optional<APInt> {
        std::optional<APInt> condition = input(0);
        if (!condition)
          return std::nullopt;
        if (!condition->isZero())
          return all;
        return unite({1, 2});
      })
      .Case<SelectLikeOpInterface>([&](auto select) -> std::optional<APInt> {
        SmallVector<unsigned, 2> indices;
        for (auto [index, value] : llvm::enumerate(op->getOperands()))
          if (value != select.getCondition() &&
              (value == select.getTrueValue() ||
               value == select.getFalseValue()))
            indices.push_back(index);
        return unite(indices);
      })
      // Bitwise operators work per bit (11.4.8); an arithmetic result is all
      // X for any X/Z operand bit (11.4.3), and so is a division by zero.
      .Case<sim::SimLogicBinaryOp>([&](auto binary) -> std::optional<APInt> {
        switch (binary.getKind()) {
        case sim::BinaryKind::And:
          if (isKnownZero(binary.getLhs()) || isKnownZero(binary.getRhs()))
            return none;
          return unite({0, 1});
        case sim::BinaryKind::Or:
          if (isKnownAllOnes(binary.getLhs()) ||
              isKnownAllOnes(binary.getRhs()))
            return none;
          return unite({0, 1});
        case sim::BinaryKind::Xor:
        case sim::BinaryKind::Xnor:
          return unite({0, 1});
        case sim::BinaryKind::UDiv:
        case sim::BinaryKind::SDiv:
        case sim::BinaryKind::UMod:
        case sim::BinaryKind::SMod:
          if (!isKnownNonzero(binary.getRhs()))
            return all;
          return fromAny({0, 1});
        default:
          return fromAny({0, 1});
        }
      })
      .Case<sim::SimLogicUnaryOp>([&](auto unary) -> std::optional<APInt> {
        std::optional<APInt> bits = input(0);
        if (!bits)
          return std::nullopt;
        switch (unary.getKind()) {
        case sim::UnaryKind::Plus:
        case sim::UnaryKind::BitNot:
          return bits->getBitWidth() == width ? *bits : all;
        default:
          return allIf(!bits->isZero());
        }
      })
      .Case<sim::SimLogicReductionOp>(
          [&](auto) -> std::optional<APInt> { return fromAny({0}); })
      .Case<sim::SimLogicLogicalOp>([&](auto logical) -> std::optional<APInt> {
        if ((logical.getKind() == sim::LogicalKind::And &&
             (isKnownZero(logical.getLhs()) ||
              isKnownZero(logical.getRhs()))) ||
            (logical.getKind() == sim::LogicalKind::Or &&
             (isKnownAllOnes(logical.getLhs()) ||
              isKnownAllOnes(logical.getRhs()))))
          return none;
        return fromAny(logicOperands());
      })
      // Case equality decides X/Z bits (11.4.5, 11.4.6); every other
      // comparison is X for any X/Z operand bit (11.4.4, 11.4.5).
      .Case<sim::SimLogicCompareOp>([&](auto compare) -> std::optional<APInt> {
        switch (compare.getKind()) {
        case sim::CompareKind::CaseEq:
        case sim::CompareKind::CaseNe:
        case sim::CompareKind::CaseZEq:
        case sim::CompareKind::CaseXZEq:
          return none;
        default:
          return fromAny({0, 1});
        }
      })
      // An X/Z amount makes the result X (11.4.10). A constant amount moves
      // the bits and fills vacated ones with known zeros, or with the sign
      // for an arithmetic right shift; any other amount may move an X/Z bit
      // anywhere.
      .Case<sim::SimLogicShiftOp>([&](auto shift) -> std::optional<APInt> {
        std::optional<APInt> bits = input(0), amount = input(1);
        if (!bits || !amount)
          return std::nullopt;
        if (!amount->isZero() || bits->getBitWidth() != width)
          return all;
        if (bits->isZero())
          return none;
        APInt by;
        auto constant =
            shift.getAmount().template getDefiningOp<sim::SimLogicConstantOp>();
        if (constant && constant.getUnknown().isZero())
          by = constant.getValue();
        else if (!matchPattern(shift.getAmount(), m_ConstantInt(&by)))
          return all;
        unsigned count = by.getActiveBits() > 32
                             ? width
                             : std::min<uint64_t>(by.getZExtValue(), width);
        switch (shift.getKind()) {
        case sim::ShiftKind::Left:
          return bits->shl(count);
        case sim::ShiftKind::Right:
          return bits->lshr(count);
        case sim::ShiftKind::RightArith:
          return bits->ashr(count);
        }
        return all;
      })
      .Case<sim::SimLogicReplicateOp>([&](auto) -> std::optional<APInt> {
        std::optional<APInt> bits = input(0);
        if (!bits)
          return std::nullopt;
        unsigned part = bits->getBitWidth();
        if (part == 0 || width % part)
          return all;
        APInt mask = none;
        for (unsigned at = 0; at != width; at += part)
          mask.insertBits(*bits, at);
        return mask;
      })
      .Case<sim::SimLogicExtractOp>([&](auto extract) -> std::optional<APInt> {
        std::optional<APInt> bits = input(0);
        if (!bits)
          return std::nullopt;
        if (extract.getLowBit() + width > bits->getBitWidth())
          return all;
        return bits->extractBits(width, extract.getLowBit());
      })
      .Case<sim::SimLogicConcatOp>([&](auto) -> std::optional<APInt> {
        APInt mask = none;
        unsigned high = width;
        for (unsigned index = 0; index != op->getNumOperands(); ++index) {
          std::optional<APInt> bits = input(index);
          if (!bits)
            return std::nullopt;
          if (bits->getBitWidth() > high)
            return all;
          high -= bits->getBitWidth();
          mask.insertBits(*bits, high);
        }
        return mask;
      })
      .Case<sim::SimLogicInsertOp>([&](auto insert) -> std::optional<APInt> {
        std::optional<APInt> base = input(0), replacement = input(1);
        if (!base || !replacement)
          return std::nullopt;
        if (base->getBitWidth() != width ||
            insert.getLowBit() + replacement->getBitWidth() > width)
          return all;
        base->insertBits(*replacement, insert.getLowBit());
        return *base;
      })
      // Zero extension adds known bits; sign extension copies the sign.
      .Case<sim::SimLogicResizeOp>([&](auto resize) -> std::optional<APInt> {
        std::optional<APInt> bits = input(0);
        if (!bits)
          return std::nullopt;
        unsigned from = bits->getBitWidth();
        if (width <= from)
          return bits->trunc(width);
        APInt mask = bits->zext(width);
        if (resize.getIsSigned() && (*bits)[from - 1])
          mask.setBitsFrom(from);
        return mask;
      })
      .Case<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(
          [&](auto) -> std::optional<APInt> {
            std::optional<APInt> bits = input(0);
            if (!bits)
              return std::nullopt;
            return bits->getBitWidth() == width ? *bits : all;
          })
      .Default([&](Operation *) -> std::optional<APInt> { return all; });
}

/// The width of the mask of a value of `type`, or none when it has no
/// unknown plane or no bit-precise fact.
std::optional<unsigned> trackedWidth(Type type) {
  if (!hasUnknownPlane(type))
    return std::nullopt;
  std::optional<unsigned> width = bits::maskWidth(type);
  // An unpacked struct of packed leaves is tracked at its provenance offsets
  // like a small unpacked array.
  if (!width && isa<sim::UnpackedStructType>(type)) {
    std::optional<uint64_t> span = sim::getProvenanceSpan(type);
    if (span && *span <= bits::maximumDenseArrayBits)
      width = static_cast<unsigned>(*span);
  }
  if (!width || *width == 0 || *width > bits::maximumTrackedBits)
    return std::nullopt;
  return width;
}

/// Sorted, disjoint [low, high) runs of the set bits of `mask`, offset by
/// `base`.
template <typename Fn>
void forEachRun(const APInt &mask, uint64_t base, Fn fn) {
  unsigned width = mask.getBitWidth();
  for (unsigned low = 0; low < width;) {
    if (!mask[low]) {
      ++low;
      continue;
    }
    unsigned high = low + 1;
    while (high < width && mask[high])
      ++high;
    fn(base + low, base + high);
    low = high;
  }
}

/// A canonical read's bits in one dense root numbering.
struct ReadSite {
  ReadKind kind = ReadKind::Unresolved;
  unsigned root = 0;
  uint64_t low = 0, high = 0;
};

//===----------------------------------------------------------------------===//
// X/Z writes of the activation
//===----------------------------------------------------------------------===//

struct Interval {
  unsigned root;
  uint64_t low, high;
  bool operator==(const Interval &other) const {
    return root == other.root && low == other.low && high == other.high;
  }
};

/// Private allocations are numbered after every storage root, with this bit
/// set.
constexpr unsigned slotRootBit = 1u << 31;
bool isSlotRoot(unsigned root) { return root & slotRootBit; }

Range rangeOf(unsigned root, uint64_t low, uint64_t high) {
  return {root, low, high - low};
}
/// Whether one of the sorted `lanes` holds all of [low, high).
bool coversLane(ArrayRef<Range> lanes, uint64_t low, uint64_t high) {
  auto it = llvm::partition_point(
      lanes, [&](const Range &lane) { return lane.low + lane.width <= low; });
  return it != lanes.end() && it->low <= low && high <= it->low + it->width;
}
/// Whether one of the sorted `lanes` overlaps [low, high).
bool overlapsLane(ArrayRef<Range> lanes, uint64_t low, uint64_t high) {
  auto it = llvm::partition_point(
      lanes, [&](const Range &lane) { return lane.low + lane.width <= low; });
  return it != lanes.end() && it->low < high;
}
/// The bits of [low, high) inside the sorted `lanes`.
APInt laneBits(ArrayRef<Range> lanes, uint64_t low, uint64_t high) {
  APInt mask(static_cast<unsigned>(high - low), 0);
  for (auto it = llvm::partition_point(
           lanes,
           [&](const Range &lane) { return lane.low + lane.width <= low; });
       it != lanes.end() && it->low < high; ++it)
    mask.setBits(
        static_cast<unsigned>(std::max(it->low, low) - low),
        static_cast<unsigned>(std::min(it->low + it->width, high) - low));
  return mask;
}

/// Bits of design storage that an X/Z write of the activation may have
/// written on some path to a program point, as persistent sets that CFG
/// edges share, so a fused activation's thousands of roots cost a join only
/// where two paths differ.
class WrittenLattice final : public dataflow::AbstractDenseLattice {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(WrittenLattice)
  using AbstractDenseLattice::AbstractDenseLattice;

  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const WrittenLattice &>(rhs);
    if (!other.reachable)
      return ChangeResult::NoChange;
    if (!reachable) {
      assign(other);
      return ChangeResult::Change;
    }
    bool changed = false;
    if (other.allStorage && !allStorage)
      changed |= setAll() == ChangeResult::Change;
    if (!allStorage)
      changed |= storage.uniteWith(other.storage);
    changed |= slots.uniteWith(other.slots);
    changed |= entry.uniteWith(other.entry);
    changed |= overwritten.intersectWith(other.overwritten);
    return changed ? ChangeResult::Change : ChangeResult::NoChange;
  }

  ChangeResult setReachable() {
    if (reachable)
      return ChangeResult::NoChange;
    reachable = true;
    return ChangeResult::Change;
  }
  /// Every storage root and net may hold X/Z; private slots keep their facts,
  /// which nothing but their own references reaches.
  ChangeResult setAll() {
    if (allStorage)
      return ChangeResult::NoChange;
    allStorage = reachable = true;
    storage.clear();
    return ChangeResult::Change;
  }
  /// Forget storage writes, which a segment's entry check observes. Either
  /// copy of the previous segment may have filled the private slots: those
  /// in `entry` are known only where the entry check tests them, and those
  /// in `unchecked` may hold X/Z.
  void resetStorage(const RangeSet &entrySlots, const RangeSet &unchecked) {
    allStorage = false;
    reachable = true;
    storage.clear();
    entry = entrySlots;
    overwritten.clear();
    slots.uniteWith(unchecked);
  }
  /// Record that every path stored to [low, high) of storage root `root`.
  void overwrite(unsigned root, uint64_t low, uint64_t high) {
    if (low < high && !isSlotRoot(root))
      overwritten.unite({rangeOf(root, low, high)});
  }
  /// Add [low, high) of `root`.
  void add(unsigned root, uint64_t low, uint64_t high) {
    if (low >= high)
      return;
    if (isSlotRoot(root))
      slots.unite({rangeOf(root, low, high)});
    else if (!allStorage)
      storage.unite({rangeOf(root, low, high)});
  }
  /// Remove [low, high) of `root`: a known store overwrote it.
  void remove(unsigned root, uint64_t low, uint64_t high) {
    if (low >= high)
      return;
    Range range = rangeOf(root, low, high);
    entry.erase({range});
    if (isSlotRoot(root))
      slots.erase({range});
    else if (!allStorage)
      storage.erase({range});
  }
  void assign(const WrittenLattice &other) {
    reachable = other.reachable;
    allStorage = other.allStorage;
    storage = other.storage;
    slots = other.slots;
    entry = other.entry;
    overwritten = other.overwritten;
  }
  /// Replace this state, the one after an operation, with `other`, the
  /// transfer of the state before it: the point has that one predecessor,
  /// so no join is needed. Shared trees make the comparison a few pointer
  /// tests; an operation revisited over an unchanged state rebuilds the
  /// same trees.
  ChangeResult assignIfChanged(const WrittenLattice &other) {
    if (reachable == other.reachable && allStorage == other.allStorage &&
        storage.sameAs(other.storage) && slots.sameAs(other.slots) &&
        entry.sameAs(other.entry) && overwritten.sameAs(other.overwritten))
      return ChangeResult::NoChange;
    assign(other);
    return ChangeResult::Change;
  }

  bool isReachable() const { return reachable; }
  bool covers(unsigned root, uint64_t low, uint64_t high) const {
    if (allStorage && !isSlotRoot(root))
      return true;
    return coversLane(written(root).lanes(root), low, high);
  }
  bool mayWrite(unsigned root, uint64_t low, uint64_t high) const {
    if (allStorage && !isSlotRoot(root))
      return true;
    return overlapsLane(written(root).lanes(root), low, high);
  }
  /// The bits of [low, high) of `root` an X/Z write may have reached.
  APInt unknownBits(unsigned root, uint64_t low, uint64_t high) const {
    if (allStorage && !isSlotRoot(root))
      return APInt::getAllOnes(static_cast<unsigned>(high - low));
    return laneBits(written(root).lanes(root), low, high);
  }
  bool isAllStorage() const { return allStorage; }
  /// Whether every path since the segment's entry stored to storage bits
  /// [low, high).
  bool overwrites(unsigned root, uint64_t low, uint64_t high) const {
    return coversLane(overwritten.lanes(root), low, high);
  }
  /// Whether slot bits [low, high) may still hold what the segment found at
  /// its entry.
  bool fromEntry(unsigned root, uint64_t low, uint64_t high) const {
    return overlapsLane(entry.lanes(root), low, high);
  }
  void print(raw_ostream &os) const override {
    os << (allStorage ? "all storage, " : "") << storage.toRanges().size()
       << " storage ranges, " << slots.toRanges().size() << " slot ranges, "
       << entry.toRanges().size() << " entry slot ranges";
  }

private:
  const RangeSet &written(unsigned root) const {
    return isSlotRoot(root) ? slots : storage;
  }

  bool reachable = false;
  bool allStorage = false;
  /// X/Z-written bits of storage roots and of private slots.
  RangeSet storage, slots;
  /// Slot bits no store of the current segment has overwritten.
  RangeSet entry;
  /// Storage bits every path of the current segment stored to.
  RangeSet overwritten;
};

//===----------------------------------------------------------------------===//
// Shared inputs
//===----------------------------------------------------------------------===//

struct Inputs {
  Inputs(const HandleDataflowResult &handles,
         const LocalStorageAnalysis &locals,
         const LocalUnknownAnalysis::Options &options)
      : handles(handles.facts), certificates(handles.certificates),
        locals(locals), segmentation(options.segmentation),
        callees(options.callees), summary(options.summary),
        staysUnknown(options.staysUnknown) {}
  const HandleFacts &handles;
  const DenseMap<Value, HandleCertificate> &certificates;
  const LocalStorageAnalysis &locals;
  const LocalUnknownAnalysis::Segmentation *segmentation;
  const LocalUnknownAnalysis::CalleeSummaries *callees;
  bool summary;
  llvm::function_ref<bool(uint64_t, uint64_t, uint64_t)> staysUnknown;
  /// Path-sensitive ranges, solved only for a function that selects with a
  /// non-constant index.
  const ScopedIntegerRangeAnalysis *ranges = nullptr;
  DataFlowSolver *solver = nullptr;

  /// Whether a dynamic selection is valid by the ranges before it.
  bool inRange(Operation *selection) const {
    if (!ranges || !solver)
      return false;
    auto *state = solver->lookupState<ScopedRangeLattice>(
        solver->getProgramPointBefore(selection));
    return state && provesSelectionInRange(selection, *ranges, *state);
  }

  /// The dense state answers questions about one storage root at a time: at
  /// a gated read of it, and at a store to it about earlier writes. A write
  /// of a root that no such question can follow changes no answer, so it is
  /// not recorded; a kernel's thousands of written roots otherwise reach
  /// every join. Positions order the top-level operations: components of
  /// the block graph topologically, then block order, and a cyclic
  /// component may revisit any of its operations.
  DenseMap<Block *, std::pair<unsigned, bool>> components;
  DenseMap<Operation *, unsigned> positions;
  DenseMap<unsigned, std::pair<unsigned, unsigned>> lastQuery;
  Region *body = nullptr;
  std::optional<std::pair<unsigned, unsigned>> position(Operation *op) const {
    auto component = components.find(op->getBlock());
    if (component == components.end())
      return std::nullopt;
    return std::make_pair(component->second.first, positions.lookup(op));
  }
  bool mayQueryAfter(Operation *op, unsigned root) const {
    if (!body)
      return true;
    auto query = lastQuery.find(root);
    if (query == lastQuery.end())
      return false;
    Operation *top = body->findAncestorOpInRegion(*op);
    if (!top)
      return true;
    std::optional<std::pair<unsigned, unsigned>> at = position(top);
    if (!at)
      return false;
    if (query->second.first != at->first)
      return query->second.first > at->first;
    if (components.lookup(top->getBlock()).second)
      return true;
    // A write inside a region operation may be followed by a query in the
    // same operation.
    return top == op ? query->second.second > at->second
                     : query->second.second >= at->second;
  }

  /// Whether a write through `reference` may reach bits that a gated read
  /// loads as the constant X of `simulation.steady_unknown`.
  bool mayWriteSteady(Value reference) const {
    auto fact = handles.find(reference);
    if (!staysUnknown || fact == handles.end() || !fact->second.descriptor ||
        fact->second.resource != schedule::ComputeResourceKind::Storage)
      return false;
    // A dynamic selection's fact is its hull.
    uint64_t width =
        fact->second.width ? fact->second.width : fact->second.rootWidth;
    return width && staysUnknown(*fact->second.descriptor, fact->second.low,
                                 fact->second.low + width);
  }

  /// Whether `value` reaches `user` from another segment. Arguments of a
  /// block a segment starts at come from the previous segment.
  bool crossesSegment(Value value, Operation *user) const {
    if (!segmentation || segmentation->count < 2)
      return false;
    auto segmentOf = [&](Operation *op) {
      return segmentation->segments.lookup(op);
    };
    unsigned use = segmentOf(user);
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      Block *block = argument.getOwner();
      if (!isa<sim::SimFuncOp>(block->getParentOp()))
        return segmentOf(block->getParentOp()) != use;
      Operation *first = &block->front();
      return segmentation->begins.contains(first) || segmentOf(first) != use;
    }
    return segmentOf(value.getDefiningOp()) != use;
  }

  /// The summary of a call's callee, or null when it is unknown.
  const LocalUnknownAnalysis::CalleeSummary *lookupCallee(Operation *op) {
    auto call = dyn_cast<sim::SimCallOp>(op);
    if (!call || !callees)
      return nullptr;
    auto found = callees->find(call.getCallee());
    return found == callees->end() ? nullptr : &found->second;
  }
  /// Canonical reads and their static classification; `Gated` here means a
  /// candidate the entry check may cover.
  DenseMap<Operation *, ReadSite> sites;
  DenseMap<std::pair<unsigned, uint64_t>, unsigned> roots;
  /// Dense indices of the function's private allocations.
  DenseMap<Operation *, uint64_t> slots;
  /// Every bit of the private allocations a segment's entry check can test,
  /// and of the others, at a segment's entry.
  RangeSet entrySlots, uncheckedSlots;
  DenseSet<unsigned> unchecked;

  /// The selectors each gated dynamic selection is valid for once they are
  /// known (HandleCertificate::knownSelectors).
  DenseMap<Operation *, SmallVector<Value, 2>> selectors;
  /// Gated reads of steady pattern bits: four-state in the copy as well.
  DenseSet<Operation *> steady;

  /// Every bit of a private allocation.
  Interval wholeSlot(sim::SimRefAllocOp allocation) {
    auto slot = slots.try_emplace(allocation, slots.size()).first->second;
    std::optional<uint64_t> span = sim::getProvenanceSpan(
        cast<sim::RefType>(allocation.getType()).getElementType());
    return {static_cast<unsigned>(slot) | slotRootBit, 0,
            span ? *span : ~uint64_t{0} >> 1};
  }
  /// The bits of a private allocation an access through `reference` reaches.
  std::optional<Interval> resolveSlot(Value reference) {
    sim::SimRefAllocOp allocation = locals.lookup(reference);
    if (!allocation)
      return std::nullopt;
    Interval whole = wholeSlot(allocation);
    auto fact = handles.find(reference);
    if (fact == handles.end() ||
        fact->second.resource != schedule::ComputeResourceKind::Local ||
        fact->second.dynamic)
      return whole;
    uint64_t width =
        fact->second.width ? fact->second.width : fact->second.rootWidth;
    if (!width || fact->second.low > whole.high ||
        width > whole.high - fact->second.low)
      return whole;
    return Interval{whole.root, fact->second.low, fact->second.low + width};
  }

  unsigned rootIndex(schedule::ComputeResourceKind resource,
                     uint64_t descriptor) {
    return roots
        .try_emplace({static_cast<unsigned>(resource), descriptor},
                     roots.size())
        .first->second;
  }
};

/// The written root bits of a reference: false when the reference may
/// address something other than one fixed design root.
bool resolveWrite(Inputs &inputs, Value reference, Interval &interval) {
  auto fact = inputs.handles.find(reference);
  if (fact == inputs.handles.end() || !fact->second.descriptor ||
      fact->second.resource == schedule::ComputeResourceKind::Unknown)
    return false;
  uint64_t width =
      fact->second.width ? fact->second.width : fact->second.rootWidth;
  interval = {inputs.rootIndex(fact->second.resource, *fact->second.descriptor),
              fact->second.low, fact->second.low + width};
  return width != 0;
}

/// Whether `op` may write design storage or a net in a way the dense
/// transfer does not model: unknown effects, foreign code and overrides.
bool mayWriteUnmodeled(Operation *op) {
  // Nested operations are visited themselves.
  if (isa<sim::SimNBAEnqueueOp, sim::SimRefAllocOp, RegionBranchOpInterface>(
          op))
    return false;
  auto effects = dyn_cast<MemoryEffectOpInterface>(op);
  if (!effects)
    return !isMemoryEffectFree(op);
  SmallVector<MemoryEffects::EffectInstance> instances;
  effects.getEffects(instances);
  return llvm::any_of(instances, [](const MemoryEffects::EffectInstance &e) {
    if (!isa<MemoryEffects::Write>(e.getEffect()))
      return false;
    SideEffects::Resource *resource = e.getResource();
    return isa<sim::StorageResource, sim::NetResource, sim::ExternalResource,
               SideEffects::DefaultResource>(resource);
  });
}

/// The X/Z writes `op` observes, given those before it: the entry check of a
/// segment, which runs before its first operation, observes every earlier
/// storage write (WrittenLattice::resetStorage).
WrittenLattice observedWrites(const WrittenLattice &before, Operation *op,
                              const Inputs &inputs) {
  WrittenLattice observed(before.getAnchor());
  observed.assign(before);
  if (inputs.segmentation && inputs.segmentation->begins.contains(op))
    observed.resetStorage(inputs.entrySlots, inputs.uncheckedSlots);
  return observed;
}

//===----------------------------------------------------------------------===//
// Analyses
//===----------------------------------------------------------------------===//

template <typename LatticeT, bool Gated>
class DomainAnalysis final
    : public dataflow::SparseForwardDataFlowAnalysis<LatticeT> {
  using Base = dataflow::SparseForwardDataFlowAnalysis<LatticeT>;

public:
  DomainAnalysis(DataFlowSolver &solver, Inputs &inputs)
      : Base(solver), inputs(inputs) {}

  LogicalResult visitOperation(Operation *op,
                               ArrayRef<const LatticeT *> operands,
                               ArrayRef<LatticeT *> results) override {
    auto put = [&](LatticeT *lattice, MayUnknownBits bits) {
      this->propagateIfChanged(lattice, lattice->join(bits));
    };
    auto everything = [&] {
      for (LatticeT *lattice : results)
        if (hasUnknownPlane(cast<Value>(lattice->getAnchor()).getType()))
          put(lattice, MayUnknownBits::all());
        else
          put(lattice, MayUnknownBits::of(APInt(1, 0)));
      return success();
    };
    // A value without a bit-precise fact keeps a one-bit summary.
    if (isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op)) {
      if (!hasUnknownPlane(op->getResult(0).getType()))
        return everything();
      unsigned width = trackedWidth(op->getResult(0).getType()).value_or(1);
      std::optional<APInt> mask = readMask(op, width);
      if (mask)
        put(results[0], MayUnknownBits::of(*mask));
      return success();
    }
    if (results.size() != 1 || !hasUnknownPlane(op->getResult(0).getType()))
      return everything();
    unsigned width = trackedWidth(op->getResult(0).getType()).value_or(1);
    auto input = [&](unsigned index) -> std::optional<APInt> {
      Value operand = op->getOperand(index);
      unsigned operandWidth = trackedWidth(operand.getType()).value_or(1);
      if (!hasUnknownPlane(operand.getType()))
        return APInt(operandWidth, 0);
      const MayUnknownBits &value = operands[index]->getValue();
      if (value.isBottom())
        return std::nullopt;
      if (value.isKnown())
        return APInt(operandWidth, 0);
      return value.get(operandWidth);
    };
    auto inRange = [&](Operation *selection) {
      return isConstantInRangeSelection(selection) || inputs.inRange(selection);
    };
    std::optional<APInt> mask = transferMayUnknown(op, width, input, inRange);
    if (mask)
      put(results[0], MayUnknownBits::of(*mask));
    return success();
  }

  void visitExternalCall(CallOpInterface call,
                         ArrayRef<const LatticeT *> arguments,
                         ArrayRef<LatticeT *> results) override {
    // A summarized callee returns known results for known arguments.
    const auto *callee = inputs.lookupCallee(call);
    bool known = callee && callee->knownResults;
    for (auto [operand, lattice] :
         llvm::zip(call.getArgOperands(), arguments)) {
      if (!hasUnknownPlane(operand.getType()))
        continue;
      const MayUnknownBits &bits = lattice->getValue();
      if (bits.isBottom())
        return;
      known &= bits.isKnown();
    }
    for (LatticeT *lattice : results) {
      Value result = cast<Value>(lattice->getAnchor());
      unsigned width = trackedWidth(result.getType()).value_or(1);
      this->propagateIfChanged(
          lattice, lattice->join(known || !hasUnknownPlane(result.getType())
                                     ? MayUnknownBits::of(APInt(width, 0))
                                     : MayUnknownBits::all()));
    }
  }

  void setToEntryState(LatticeT *lattice) override {
    auto value = dyn_cast<Value>(lattice->getAnchor());
    // A summary assumes known arguments of the function itself.
    auto argument = dyn_cast_or_null<BlockArgument>(value);
    bool knownArgument =
        inputs.summary && argument && argument.getOwner()->isEntryBlock() &&
        isa<sim::SimFuncOp>(argument.getOwner()->getParentOp());
    if (!value || !hasUnknownPlane(value.getType()))
      return this->propagateIfChanged(
          lattice, lattice->join(MayUnknownBits::of(APInt(1, 0))));
    unsigned width = trackedWidth(value.getType()).value_or(1);
    this->propagateIfChanged(
        lattice,
        lattice->join(knownArgument ? MayUnknownBits::of(APInt(width, 0))
                                    : MayUnknownBits::all()));
  }

private:
  /// The may-X/Z bits a read observes, or none while they are unsolved.
  std::optional<APInt> readMask(Operation *op, unsigned width) {
    const APInt all = APInt::getAllOnes(width), none = APInt(width, 0);
    auto site = inputs.sites.find(op);
    if (site != inputs.sites.end() &&
        (site->second.kind == ReadKind::TwoState ||
         (Gated && site->second.kind == ReadKind::Checked)))
      return none;
    if constexpr (!Gated) {
      return all;
    } else {
      // A gated read observes entry state; a private read, whatever the
      // activation stored. Either holds X/Z only where a write may have
      // put it.
      if (site == inputs.sites.end() ||
          (site->second.kind != ReadKind::Gated &&
           site->second.kind != ReadKind::Local) ||
          (inputs.summary && site->second.kind == ReadKind::Gated) ||
          inputs.steady.contains(op))
        return all;
      // A dynamic selection reads its gated hull only while it is valid:
      // whenever its selectors are known (IEEE 1800-2023 7.4.6, 11.5.1).
      if (auto selectors = inputs.selectors.find(op);
          selectors != inputs.selectors.end())
        for (Value selector : selectors->second) {
          if (!hasUnknownPlane(selector.getType()))
            continue;
          const MayUnknownBits &bits =
              this->getLatticeElementFor(this->getProgramPointAfter(op),
                                         selector)
                  ->getValue();
          if (bits.isBottom())
            return std::nullopt;
          if (!bits.isKnown())
            return all;
        }
      const auto *before = this->template getOrCreateFor<WrittenLattice>(
          this->getProgramPointAfter(op), this->getProgramPointBefore(op));
      WrittenLattice observed = observedWrites(*before, op, inputs);
      const ReadSite &read = site->second;
      // A read that selects dynamically observes its hull; a shared mask
      // covers every leaf. Both are all or nothing.
      if (read.high - read.low != width ||
          bits::isElementwise(op->getResult(0).getType()))
        return observed.mayWrite(read.root, read.low, read.high) ? all : none;
      return observed.unknownBits(read.root, read.low, read.high);
    }
  }

  Inputs &inputs;
};

/// The source of a two-state view `logic.from_bits(logic.to_bits(x))`, also
/// through `packed.flatten`/`packed.unflatten`, or null.
Value viewedKnown(Value view) {
  Value flat = view;
  if (auto unflatten = view.getDefiningOp<sim::SimPackedUnflattenOp>())
    flat = unflatten.getInput();
  auto fromBits = flat.getDefiningOp<sim::SimLogicFromBitsOp>();
  if (!fromBits)
    return nullptr;
  auto toBits = fromBits.getInput().getDefiningOp<sim::SimLogicToBitsOp>();
  if (!toBits)
    return nullptr;
  Value source = toBits.getInput();
  if (flat != view) {
    auto flatten = source.getDefiningOp<sim::SimPackedFlattenOp>();
    if (!flatten)
      return nullptr;
    source = flatten.getInput();
  }
  return source;
}

/// Constant propagation that also decides the tests lowering emits for
/// X/Z in a value: `x === view(x)` is true exactly when `x` holds no X/Z
/// (IEEE 1800-2023 11.4.6), so in the gated copy it is true for a known `x`
/// and the paths that merge X/Z are dead there.
class KnownConstantPropagation final
    : public dataflow::SparseConstantPropagation {
  using ConstantLattice = dataflow::Lattice<dataflow::ConstantValue>;

public:
  KnownConstantPropagation(DataFlowSolver &solver, Inputs &)
      : SparseConstantPropagation(solver) {}

  LogicalResult visitOperation(Operation *op,
                               ArrayRef<const ConstantLattice *> operands,
                               ArrayRef<ConstantLattice *> results) override {
    auto compare = dyn_cast<sim::SimLogicCompareOp>(op);
    if (compare && (compare.getKind() == sim::CompareKind::CaseEq ||
                    compare.getKind() == sim::CompareKind::CaseNe)) {
      Value tested;
      if (viewedKnown(compare.getRhs()) == compare.getLhs())
        tested = compare.getLhs();
      else if (viewedKnown(compare.getLhs()) == compare.getRhs())
        tested = compare.getRhs();
      if (tested && hasUnknownPlane(tested.getType())) {
        const MayUnknownBits &bits =
            getOrCreateFor<GatedLattice>(getProgramPointAfter(op), tested)
                ->getValue();
        if (bits.isBottom())
          return success();
        if (bits.isKnown()) {
          bool equal = compare.getKind() == sim::CompareKind::CaseEq;
          propagateIfChanged(
              results[0],
              results[0]->join(dataflow::ConstantValue(
                  BoolAttr::get(op->getContext(), equal), op->getDialect())));
          return success();
        }
      }
    }
    return SparseConstantPropagation::visitOperation(op, operands, results);
  }
};

/// The bits X/Z writes of the activation may have stored so far.
class WrittenAnalysis final
    : public dataflow::DenseForwardDataFlowAnalysis<WrittenLattice> {
public:
  WrittenAnalysis(DataFlowSolver &solver, Inputs &inputs)
      : DenseForwardDataFlowAnalysis(solver), inputs(inputs) {}

  LogicalResult visitOperation(Operation *op, const WrittenLattice &before,
                               WrittenLattice *after) override {
    if (!before.isReachable())
      return success();
    WrittenLattice next = observedWrites(before, op, inputs);
    // A store of a possibly X/Z value adds its bits; a known store to an
    // exact location removes them (a value not yet solved counts as known
    // until it is).
    auto write = [&](Value reference, Value value) {
      Interval interval;
      bool exact;
      if (std::optional<Interval> slot = inputs.resolveSlot(reference)) {
        interval = *slot;
        auto fact = inputs.handles.find(reference);
        exact = fact != inputs.handles.end() && !fact->second.dynamic &&
                fact->second.resource == schedule::ComputeResourceKind::Local;
      } else if (resolveWrite(inputs, reference, interval)) {
        if (!inputs.mayQueryAfter(op, interval.root))
          return;
        exact = !inputs.handles.lookup(reference).dynamic;
      } else {
        (void)next.setAll();
        return;
      }
      // A write of steady-unknown bits may make them known, so a gated read
      // after it cannot load them as the pattern's constant X. A value not
      // yet solved counts as known until it is. The interval may span a
      // whole memory root; only an exact store of a tracked value of the
      // interval's width has per-bit facts. `unknown` holds the bits the
      // store may make X/Z from the interval's low bit, and none stands for
      // all of them.
      uint64_t width = interval.high - interval.low;
      std::optional<APInt> unknown;
      if (value) {
        if (!hasUnknownPlane(value.getType()))
          unknown = APInt::getZero(0);
        else if (!inputs.mayWriteSteady(reference)) {
          const MayUnknownBits &bits =
              getOrCreateFor<GatedLattice>(getProgramPointAfter(op), value)
                  ->getValue();
          std::optional<unsigned> tracked = trackedWidth(value.getType());
          if (bits.isBottom() || bits.isKnown())
            unknown = APInt::getZero(0);
          else if (exact && tracked && *tracked == width &&
                   !bits::isElementwise(value.getType()))
            unknown = bits.get(*tracked);
        }
      }
      if (!exact) {
        if (!unknown)
          next.add(interval.root, interval.low, interval.high);
        return;
      }
      next.remove(interval.root, interval.low, interval.high);
      if (!unknown)
        next.add(interval.root, interval.low, interval.high);
      else
        forEachRun(*unknown, interval.low, [&](uint64_t low, uint64_t high) {
          next.add(interval.root, low, high);
        });
      next.overwrite(interval.root, interval.low, interval.high);
    };
    // Each execution of an allocation starts a lifetime holding its
    // initializer.
    if (auto allocation = dyn_cast<sim::SimRefAllocOp>(op))
      if (inputs.locals.lookup(allocation.getResult()))
        write(allocation.getResult(), allocation.getInitialValue());
    if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
      write(store.getReference(), store.getValue());
    else if (auto copy = dyn_cast<sim::SimRefCopyOp>(op))
      write(copy.getDestination(), Value());
    else if (auto netWrite = dyn_cast<sim::SimNetWriteOp>(op))
      write(netWrite.getNet(), netWrite.getValue());
    else if (auto drive = dyn_cast<sim::SimDriverDriveOp>(op))
      write(drive.getDriver(), drive.getValue());
    else if (mayWriteUnmodeled(op))
      (void)next.setAll();
    propagateIfChanged(after, after->assignIfChanged(next));
    return success();
  }

  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const WrittenLattice &before,
                                    WrittenLattice *after) override {
    // A callee that may write design storage may write X/Z anywhere. One
    // that stores only through its formals may put X/Z into what they
    // reference, unless its summary proves those stores known for known
    // arguments (a value not yet solved counts as known until it is).
    if (!before.isReachable())
      return;
    WrittenLattice next(after->getAnchor());
    next.assign(before);
    const auto *callee = inputs.lookupCallee(call);
    if (!callee || callee->writes) {
      (void)next.setAll();
    } else if (!callee->storedFormals.empty()) {
      Operation *op = call.getOperation();
      auto arguments = call.getArgOperands();
      bool known = callee->knownFormalStores;
      if (known)
        for (Value argument : arguments) {
          if (!hasUnknownPlane(argument.getType()))
            continue;
          const MayUnknownBits &bits =
              getOrCreateFor<GatedLattice>(getProgramPointAfter(op), argument)
                  ->getValue();
          if (!bits.isBottom() && !bits.isKnown()) {
            known = false;
            break;
          }
        }
      for (unsigned formal : callee->storedFormals) {
        if (formal >= arguments.size()) {
          (void)next.setAll();
          break;
        }
        Value reference = arguments[formal];
        Interval interval;
        if (std::optional<Interval> slot = inputs.resolveSlot(reference))
          interval = *slot;
        else if (!resolveWrite(inputs, reference, interval)) {
          (void)next.setAll();
          break;
        }
        if ((!known || inputs.mayWriteSteady(reference)) &&
            (isSlotRoot(interval.root) ||
             inputs.mayQueryAfter(call.getOperation(), interval.root)))
          next.add(interval.root, interval.low, interval.high);
      }
    }
    propagateIfChanged(after, after->assignIfChanged(next));
  }

private:
  void setToEntryState(WrittenLattice *lattice) override {
    propagateIfChanged(lattice, lattice->setReachable());
  }

  Inputs &inputs;
};

/// Approximate unknown-plane work that depends on `value` within its
/// function: the operations its four-state uses reach through results and
/// branch arguments, up to a budget. Stores end a chain.
unsigned downstreamWork(Value value, unsigned budget = 256) {
  unsigned work = 0, visited = 0;
  SmallVector<Value> pending{value};
  llvm::SmallPtrSet<Operation *, 16> seen;
  while (!pending.empty() && visited < budget) {
    Value next = pending.pop_back_val();
    for (Operation *user : next.getUsers()) {
      if (!seen.insert(user).second || ++visited > budget)
        continue;
      if (isa<sim::SimRefStoreOp, sim::SimNBAEnqueueOp, CallOpInterface>(
              user))
        continue;
      work += getUnknownPlaneWork(user);
      if (auto branch = dyn_cast<BranchOpInterface>(user)) {
        for (unsigned index = 0; index != user->getNumSuccessors(); ++index) {
          SuccessorOperands operands = branch.getSuccessorOperands(index);
          for (unsigned position = 0; position != operands.size(); ++position)
            if (operands[position] == next)
              pending.push_back(
                  user->getSuccessor(index)->getArgument(position));
        }
        continue;
      }
      for (Value result : user->getResults())
        if (hasUnknownPlane(result.getType()))
          pending.push_back(result);
    }
  }
  return work;
}

bool isExecutable(DataFlowSolver &solver, Block *block) {
  auto *state = solver.lookupState<dataflow::Executable>(
      solver.getProgramPointBefore(block));
  return state && state->isLive();
}

} // namespace

StringRef LocalUnknownAnalysis::stringify(ReadKind kind) {
  switch (kind) {
  case ReadKind::Gated:
    return "gated";
  case ReadKind::TwoState:
    return "two-state";
  case ReadKind::Dead:
    return "dead";
  case ReadKind::Local:
    return "local";
  case ReadKind::Unresolved:
    return "unresolved";
  case ReadKind::Dynamic:
    return "dynamic";
  case ReadKind::Written:
    return "written";
  case ReadKind::Checked:
    return "checked";
  }
  llvm_unreachable("unknown read kind");
}

LocalUnknownAnalysis::ReadKind
LocalUnknownAnalysis::getKind(Operation *read) const {
  auto found = kinds.find(read);
  return found == kinds.end() ? ReadKind::Unresolved : found->second;
}

LocalUnknownAnalysis::LocalUnknownAnalysis(sim::SimFuncOp function,
                                           const HandleDataflowResult &handles,
                                           const Options &options) {
  const Segmentation *segmentation = options.segmentation;
  if (function.isExternal())
    return;
  // The solvers follow region-based control flow through its interface.
  bool nested = false;
  SmallVector<sim::SimRefStoreOp> stores;
  function.walk([&](Operation *op) {
    if (op != function.getOperation() && op->getNumRegions() &&
        !isa<RegionBranchOpInterface>(op))
      nested = true;
    if (isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op))
      reads.push_back(op);
    if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
      stores.push_back(store);
  });
  if (nested) {
    failure = "nested region";
    for (Operation *read : reads)
      kinds[read] = ReadKind::Unresolved;
    return;
  }

  LocalStorageAnalysis locals(function,
                              LocalStorageAnalysis::Lifetime::Invocation,
                              /*passesToCalls=*/true);
  Inputs inputs(handles, locals, options);
  DenseMap<unsigned, uint64_t> descriptors;
  for (Operation *read : reads) {
    Value result = read->getResult(0);
    ReadSite site;
    Value reference = read->getOperand(0);
    auto fact = handles.facts.find(reference);
    if (!hasUnknownPlane(result.getType()) ||
        (options.provenKnownRead && options.provenKnownRead(read)))
      site.kind = ReadKind::TwoState;
    else if (std::optional<Interval> slot = inputs.resolveSlot(reference)) {
      site.kind = ReadKind::Local;
      site.root = slot->root;
      site.low = slot->low;
      site.high = slot->high;
    } else if (!isa<sim::SimRefLoadOp>(read) || fact == handles.facts.end() ||
               !fact->second.descriptor ||
               fact->second.resource != schedule::ComputeResourceKind::Storage)
      site.kind = ReadKind::Unresolved;
    else {
      uint64_t width =
          fact->second.width ? fact->second.width : fact->second.rootWidth;
      // A dynamic selection is gated over its hull, and only where it is
      // valid for every known value of its selectors (IEEE 1800-2023 7.4.5,
      // 11.5.1).
      auto certificate = handles.certificates.find(reference);
      bool steady =
          options.staysUnknown &&
          options.staysUnknown(*fact->second.descriptor, fact->second.low,
                               fact->second.low + width);
      if (width == 0)
        site.kind = ReadKind::Unresolved;
      else if (fact->second.dynamic &&
               (certificate == handles.certificates.end() ||
                !certificate->second.inBoundsIfKnown || steady))
        site.kind = ReadKind::Dynamic;
      else {
        if (steady)
          inputs.steady.insert(read);
        site.kind = ReadKind::Gated;
        site.root = inputs.rootIndex(fact->second.resource,
                                     *fact->second.descriptor);
        site.low = fact->second.low;
        site.high = fact->second.low + width;
        descriptors[site.root] = *fact->second.descriptor;
        if (fact->second.dynamic && !certificate->second.knownSelectors.empty())
          inputs.selectors[read] = certificate->second.knownSelectors;
      }
    }
    inputs.sites[read] = site;
  }

  // A segment's entry check tests the narrow slots its known reads find as
  // the previous segment left them.
  DenseMap<unsigned, sim::SimRefAllocOp> slotAllocations;
  if (segmentation && segmentation->count > 1) {
    Ranges checkable, unchecked;
    function.walk([&](sim::SimRefAllocOp allocation) {
      if (!locals.lookup(allocation.getResult()))
        return;
      Interval whole = inputs.wholeSlot(allocation);
      slotAllocations[whole.root] = allocation;
      Type type = allocation.getType().getElementType();
      if (KnownVersioningAnalysis::isCastable(type) &&
          whole.high <= maximumCheckedSlotBits) {
        checkable.push_back(rangeOf(whole.root, whole.low, whole.high));
      } else {
        unchecked.push_back(rangeOf(whole.root, whole.low, whole.high));
        inputs.unchecked.insert(whole.root);
      }
    });
    auto byRoot = [](const Range &a, const Range &b) {
      return a.root < b.root;
    };
    llvm::sort(checkable, byRoot);
    llvm::sort(unchecked, byRoot);
    inputs.entrySlots.unite(checkable);
    inputs.uncheckedSlots.unite(unchecked);
  }

  unsigned count = segmentation ? segmentation->count : 1;
  auto segmentOf = [&](Operation *op) {
    return segmentation ? segmentation->segments.lookup(op) : 0u;
  };
  segments.resize(count);
  // Reads tested where they load: the copy continues in the original body at
  // the read when it holds X/Z. Only reads at the body's top level qualify,
  // since a branch cannot leave a structured region, and only where enough
  // four-state work depends on the read to pay for the test.
  constexpr unsigned minimumCheckedWork = 16;
  auto checkable = [&](Operation *read) {
    return !options.summary && options.checks &&
           read->getParentRegion() == &function.getBody() &&
           KnownVersioningAnalysis::isCastable(read->getResult(0).getType()) &&
           downstreamWork(read->getResult(0)) >= minimumCheckedWork;
  };
  for (Operation *read : reads) {
    ReadSite &site = inputs.sites[read];
    if ((site.kind == ReadKind::Dynamic || site.kind == ReadKind::Unresolved) &&
        checkable(read))
      site.kind = ReadKind::Checked;
  }

  // Where the dense state is asked about each storage root (Inputs::
  // mayQueryAfter): gated reads, and stores testing earlier writes.
  {
    SmallVector<std::pair<SmallVector<Block *>, bool>> components;
    for (auto scc = llvm::scc_begin(&function.getBody()); !scc.isAtEnd(); ++scc)
      components.push_back(
          {SmallVector<Block *>(scc->begin(), scc->end()), scc.hasCycle()});
    // Tarjan's order lists a component after every component it reaches.
    for (auto [index, component] : llvm::enumerate(llvm::reverse(components)))
      for (Block *block : component.first)
        inputs.components[block] = {index, component.second};
    for (Block &block : function.getBody())
      for (auto [index, op] : llvm::enumerate(block))
        inputs.positions[&op] = index;
    inputs.body = &function.getBody();
    auto query = [&](Operation *op, unsigned root) {
      Operation *top = function.getBody().findAncestorOpInRegion(*op);
      std::optional<std::pair<unsigned, unsigned>> at =
          top ? inputs.position(top) : std::nullopt;
      if (!at)
        return;
      auto [found, inserted] = inputs.lastQuery.try_emplace(root, *at);
      if (!inserted && found->second < *at)
        found->second = *at;
    };
    for (Operation *read : reads)
      if (const ReadSite &site = inputs.sites[read];
          site.kind == ReadKind::Gated)
        query(read, site.root);
    for (sim::SimRefStoreOp store : stores) {
      Interval interval;
      if (resolveWrite(inputs, store.getReference(), interval))
        query(store, interval.root);
    }
  }

  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<KnownConstantPropagation>(inputs);
  // Dynamic selections with a non-constant index are X/Z when invalid;
  // path-sensitive ranges prove the common loop-indexed ones valid.
  if (function
          .walk([](Operation *op) {
            return isa<sim::SimLogicDynExtractOp, sim::SimArrayDynExtractOp>(
                       op) &&
                           !isConstantInRangeSelection(op)
                       ? WalkResult::interrupt()
                       : WalkResult::advance();
          })
          .wasInterrupted()) {
    inputs.ranges = solver.load<ScopedIntegerRangeAnalysis>(function);
    inputs.solver = &solver;
  }
  solver.load<DomainAnalysis<GatedLattice, true>>(inputs);
  solver.load<DomainAnalysis<BaselineLattice, false>>(inputs);
  solver.load<WrittenAnalysis>(inputs);
  if (failed(solver.initializeAndRun(function))) {
    failure = "domain analysis failed";
    return;
  }

  auto knownIn = [&](auto *lattice) {
    return lattice && lattice->getValue().isKnown();
  };
  auto knownInGatedCopy = [&](Value value) {
    return !hasUnknownPlane(value.getType()) ||
           knownIn(solver.lookupState<GatedLattice>(value));
  };
  // The writes `op` observes; without a solved state, assume private slots
  // hold entry contents and storage was neither written nor overwritten.
  auto observedBefore = [&](Operation *op) -> std::optional<WrittenLattice> {
    const auto *before =
        solver.lookupState<WrittenLattice>(solver.getProgramPointBefore(op));
    if (!before)
      return std::nullopt;
    return observedWrites(*before, op, inputs);
  };
  SmallVector<llvm::SetVector<uint64_t>> gated(count);
  for (Operation *read : reads) {
    ReadSite site = inputs.sites.lookup(read);
    ReadKind kind = site.kind;
    if (!isExecutable(solver, read->getBlock())) {
      kind = ReadKind::Dead;
    } else if (kind == ReadKind::Gated && inputs.steady.contains(read)) {
      // Its value is four-state anyway; it stays value-only unless an X/Z
      // write of the segment, or any write of its pattern bits, may reach
      // it.
      std::optional<WrittenLattice> observed = observedBefore(read);
      if (!observed || observed->mayWrite(site.root, site.low, site.high))
        kind = ReadKind::Written;
    } else if (kind == ReadKind::Gated && !knownInGatedCopy(read->getResult(0)))
      kind = llvm::all_of(inputs.selectors.lookup(read), knownInGatedCopy)
                 ? ReadKind::Written
                 : ReadKind::Dynamic;
    kinds[read] = kind;
    if (kind == ReadKind::Checked)
      ++segments[segmentOf(read)].checks;
    if (kind != ReadKind::Local && kind != ReadKind::Gated)
      continue;
    std::optional<WrittenLattice> observed = observedBefore(read);
    // A known private read whose bits may come from before its segment
    // needs the slot tested at the segment's entry.
    if (kind == ReadKind::Local) {
      if (segmentation && knownInGatedCopy(read->getResult(0)) &&
          !inputs.unchecked.contains(site.root) &&
          (!observed || observed->fromEntry(site.root, site.low, site.high)))
        if (sim::SimRefAllocOp allocation = slotAllocations.lookup(site.root))
          segments[segmentOf(read)].slotChecks.insert(allocation);
      continue;
    }
    unsigned segment = segmentOf(read);
    segments[segment].benefit += 1 + (site.high - site.low) / 64;
    // Bits every path of the segment stored before the read need no check.
    if (observed && observed->overwrites(site.root, site.low, site.high))
      continue;
    gated[segment].insert(descriptors.lookup(site.root));
  }

  // A known store writes only the value plane of a tested root whose bits no
  // X/Z write of the segment may have reached: they stay zero. A write with
  // an invalid index writes nothing (7.4.5), so a dynamic one qualifies too.
  // Steady-unknown bits hold X/Z at the check, so a store reaching them
  // keeps both planes.
  if (!options.summary)
    for (sim::SimRefStoreOp store : stores) {
      Value reference = store.getReference();
      auto fact = handles.facts.find(reference);
      if (!isExecutable(solver, store->getBlock()) ||
          !hasUnknownPlane(
              cast<sim::RefType>(reference.getType()).getElementType()) ||
          !knownInGatedCopy(store.getValue()) ||
          inputs.resolveSlot(reference) || inputs.mayWriteSteady(reference) ||
          fact == handles.facts.end() || !fact->second.descriptor ||
          fact->second.resource != schedule::ComputeResourceKind::Storage)
        continue;
      uint64_t width =
          fact->second.width ? fact->second.width : fact->second.rootWidth;
      unsigned root =
          inputs.rootIndex(fact->second.resource, *fact->second.descriptor);
      std::optional<WrittenLattice> observed = observedBefore(store);
      if (width == 0 || !observed ||
          observed->mayWrite(root, fact->second.low, fact->second.low + width))
        continue;
      knownStores.insert(store);
      Segment &segment = segments[segmentOf(store)];
      segment.benefit += 1 + width / 64;
      gated[segmentOf(store)].insert(*fact->second.descriptor);
    }

  // Each operation with a four-state operand or result that the copy makes
  // two-state stops computing its unknown plane.
  auto record = [&](Value value) {
    if (!hasUnknownPlane(value.getType()))
      return;
    if (knownIn(solver.lookupState<GatedLattice>(value)))
      knownInCopy.insert(value);
    if (knownIn(solver.lookupState<BaselineLattice>(value)))
      knownInOriginal.insert(value);
  };
  function.walk([&](Block *block) {
    if (!isExecutable(solver, block))
      deadBlocks.insert(block);
    for (BlockArgument argument : block->getArguments())
      record(argument);
  });
  function.walk([&](Operation *op) {
    for (Value result : op->getResults())
      record(result);
  });
  auto improved = [&](Value value) {
    return knownInCopy.contains(value) && !knownInOriginal.contains(value);
  };
  function.walk([&](Operation *op) {
    if (op == function.getOperation() || !op->getBlock() ||
        !isExecutable(solver, op->getBlock()))
      return;
    Segment &segment = segments[segmentOf(op)];
    if (!isa<sim::SimRefLoadOp>(op) &&
        (llvm::any_of(op->getResults(), improved) ||
         llvm::any_of(op->getOperands(), improved)))
      segment.benefit += getUnknownPlaneWork(op);
    // A value from another segment comes from whichever copy ran there:
    // the entry check tests each one only the copy proves known.
    for (Value operand : op->getOperands())
      if (improved(operand) && inputs.crossesSegment(operand, op)) {
        segment.liveIns.insert(operand);
        segment.untestable |= !sim::hasFixedBitValue(operand.getType());
      }
  });

  for (auto [segment, roots] : llvm::enumerate(gated)) {
    SmallVector<uint64_t, 0> &gate = segments[segment].gate;
    gate.assign(roots.begin(), roots.end());
    llvm::sort(gate);
  }
}

LocalUnknownAnalysis::CalleeSummaries
LocalUnknownAnalysis::summarize(sim::SimDesignOp design,
                                const HandleDataflowAnalysis &handles) {
  CalleeSummaries summaries;
  if (design.getBody().empty())
    return summaries;
  // Only called functions need summaries. A large callee keeps the
  // conservative default rather than a whole-body solve per caller design.
  constexpr unsigned maximumSummarizedOperations = 4096;
  llvm::StringSet<> called;
  design.walk([&](sim::SimCallOp call) { called.insert(call.getCallee()); });
  llvm::StringSet<> functions;
  for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
    if (!called.contains(function.getSymName()))
      continue;
    unsigned size = 0;
    function.walk([&](Operation *) { ++size; });
    if (size <= maximumSummarizedOperations)
      functions.insert(function.getSymName());
  }
  auto summarizeOne = [&](sim::SimFuncOp function) {
    CalleeSummary summary;
    if (function.isExternal())
      return summary;
    HandleDataflowResult references = handles.analyze(function);
    LocalUnknownAnalysis analysis(function, references,
                                  Options{nullptr, &summaries, true});
    if (!analysis.getFailure().empty())
      return summary;
    LocalStorageAnalysis locals(function,
                                LocalStorageAnalysis::Lifetime::Invocation,
                                /*passesToCalls=*/true);
    // A store through a formal writes what the caller passed; every other
    // store outside private storage writes the design.
    auto formalOf = [&](Value reference) -> std::optional<unsigned> {
      auto fact = references.facts.find(reference);
      if (fact == references.facts.end() || fact->second.descriptor ||
          !fact->second.formal)
        return std::nullopt;
      return fact->second.formal;
    };
    llvm::SmallSet<unsigned, 4> storedFormals;
    bool knownFormalStores = true;
    auto storeFormal = [&](Value reference, bool known) {
      std::optional<unsigned> formal = formalOf(reference);
      if (!formal)
        return false;
      storedFormals.insert(*formal);
      knownFormalStores &= known;
      return true;
    };
    summary.writes =
        function
            .walk([&](Operation *op) {
              bool writes = false;
              if (op == function.getOperation()) {
                return WalkResult::advance();
              } else if (auto store = dyn_cast<sim::SimRefStoreOp>(op)) {
                Value value = store.getValue();
                writes = !locals.isLocalAccess(op) &&
                         !storeFormal(store.getReference(),
                                      !hasUnknownPlane(value.getType()) ||
                                          analysis.isKnownInCopy(value));
              } else if (auto copy = dyn_cast<sim::SimRefCopyOp>(op)) {
                writes = !locals.lookup(copy.getDestination()) &&
                         !storeFormal(copy.getDestination(), false);
              } else if (auto invoke = dyn_cast<sim::SimCallOp>(op)) {
                auto callee = summaries.find(invoke.getCallee());
                if (callee == summaries.end() || callee->second.writes) {
                  writes = true;
                } else {
                  auto arguments = invoke.getArgOperands();
                  for (unsigned formal : callee->second.storedFormals) {
                    Value argument =
                        formal < arguments.size() ? arguments[formal] : Value();
                    if (!argument ||
                        (!locals.lookup(argument) &&
                         !storeFormal(argument,
                                      callee->second.knownFormalStores)))
                      writes = true;
                  }
                }
              } else {
                writes = isa<sim::SimNetWriteOp, sim::SimDriverDriveOp>(op) ||
                         mayWriteUnmodeled(op);
              }
              return writes ? WalkResult::interrupt() : WalkResult::advance();
            })
            .wasInterrupted();
    if (!summary.writes) {
      summary.storedFormals.assign(storedFormals.begin(), storedFormals.end());
      llvm::sort(summary.storedFormals);
      summary.knownFormalStores = knownFormalStores;
    }
    summary.knownResults = !function
                                .walk([&](sim::SimReturnOp returnOp) {
                                  for (Value operand : returnOp.getOperands())
                                    if (hasUnknownPlane(operand.getType()) &&
                                        !analysis.isKnownInCopy(operand))
                                      return WalkResult::interrupt();
                                  return WalkResult::advance();
                                })
                                .wasInterrupted();
    return summary;
  };
  // Callees first: the call graph's strongly connected components come in
  // post-order. A function without a summary keeps the conservative default,
  // so a recursive component stays unsummarized.
  CallGraph graph(design);
  for (auto scc = llvm::scc_begin(static_cast<const CallGraph *>(&graph));
       !scc.isAtEnd(); ++scc) {
    if (scc.hasCycle())
      continue;
    for (CallGraphNode *node : *scc) {
      if (node->isExternal())
        continue;
      auto function =
          dyn_cast<sim::SimFuncOp>(node->getCallableRegion()->getParentOp());
      if (function && functions.contains(function.getSymName()))
        summaries[function.getSymName()] = summarizeOne(function);
    }
  }
  return summaries;
}

} // namespace obelisk::analysis
