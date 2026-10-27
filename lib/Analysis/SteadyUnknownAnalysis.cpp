//===- SteadyUnknownAnalysis.cpp - Storage bits X/Z in steady state -------===//

#include "talos/Analysis/SteadyUnknownAnalysis.h"
#include "talos/Analysis/BoundedLoopAnalysis.h"
#include "talos/Analysis/HandleDataflowAnalysis.h"
#include "talos/Analysis/LocalStorageAnalysis.h"
#include "talos/Analysis/NativeStorageLayout.h"
#include "talos/Analysis/UnknownBitGeometry.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Dialect/Simulation/SimulationTypes.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

namespace talos::analysis {
namespace {

using namespace bits;

/// A reference is followed through at most this many block arguments and
/// enclosing calls to the root it addresses.
constexpr unsigned maximumResolutionDepth = 8;

/// The may-known bits of the default value of `type` (IEEE 1800-2023 6.8,
/// Table 6-7; 7.4.5, Table 7-1), or of its packed leaf for a shared mask:
/// known where it holds no X/Z.
APInt defaultKnown(Type type, unsigned width) {
  return ~defaultUnknown(type, width);
}

/// Whether writes of `function` run once, before the design reaches steady
/// state.
bool runsOnce(sim::SimFuncOp function) {
  if (!function)
    return false;
  switch (function.getEntryKind()) {
  case sim::EntryKind::RootInitializer:
  case sim::EntryKind::Initial:
  case sim::EntryKind::PortInitialize:
  case sim::EntryKind::Final:
    return true;
  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//
// Values
//===----------------------------------------------------------------------===//

/// Bottom, the may-known bits of a packed value, or every bit.
class MayKnownBits {
public:
  MayKnownBits() = default;
  static MayKnownBits of(APInt mask) {
    MayKnownBits bits;
    bits.state = mask.isAllOnes() ? State::All : State::Mask;
    bits.mask = std::move(mask);
    return bits;
  }
  static MayKnownBits all() {
    MayKnownBits bits;
    bits.state = State::All;
    return bits;
  }
  bool isBottom() const { return state == State::Bottom; }
  /// The may-known bits at `width`, all of them unless a mask of that width
  /// is known.
  APInt get(unsigned width) const {
    if (state == State::Mask && mask.getBitWidth() == width)
      return mask;
    return APInt::getAllOnes(width);
  }
  static MayKnownBits join(const MayKnownBits &lhs, const MayKnownBits &rhs) {
    if (lhs.isBottom())
      return rhs;
    if (rhs.isBottom())
      return lhs;
    if (lhs.state == State::All || rhs.state == State::All ||
        lhs.mask.getBitWidth() != rhs.mask.getBitWidth())
      return all();
    return of(lhs.mask | rhs.mask);
  }
  bool operator==(const MayKnownBits &other) const {
    return state == other.state && (state != State::Mask || mask == other.mask);
  }
  void print(raw_ostream &os) const {
    if (state == State::Bottom)
      os << "<bottom>";
    else if (state == State::All)
      os << "all";
    else
      mask.print(os, /*isSigned=*/false);
  }

private:
  enum class State { Bottom, Mask, All };
  State state = State::Bottom;
  APInt mask;
};
using MayKnownLattice = dataflow::Lattice<MayKnownBits>;

//===----------------------------------------------------------------------===//
// Roots and slots
//===----------------------------------------------------------------------===//

class RootAnchor final : public GenericLatticeAnchorBase<RootAnchor, uint64_t> {
public:
  using Base::Base;
  Location getLoc() const override { return UnknownLoc::get(nullptr); }
  void print(raw_ostream &os) const override { os << "root " << getValue(); }
};
class SlotAnchor final
    : public GenericLatticeAnchorBase<SlotAnchor, Operation *> {
public:
  using Base::Base;
  Location getLoc() const override { return getValue()->getLoc(); }
  void print(raw_ostream &os) const override { os << "slot"; }
};

/// Where an access lands in a state: at one offset for `width` bits, over
/// whole elements of a shared state, or anywhere.
struct Placement {
  enum class Kind { Offset, Elements, Lanes, Anywhere } kind = Kind::Anywhere;
  unsigned offset = 0;
  uint64_t elements = 0;
  /// The first element a fixed access of a shared state reaches.
  std::optional<uint64_t> element;
  /// The offsets of a dense state an indexed access may reach.
  SmallVector<unsigned, 4> lanes;
};

/// The bounded loops of one function and its dominance, computed on demand.
struct LoopFacts {
  explicit LoopFacts(sim::SimFuncOp function)
      : loops(analyzeBoundedLoops(function.getBody())), dominance(function) {}
  const BoundedLoop *lookup(BlockArgument induction) const {
    for (const BoundedLoop &loop : loops)
      if (loop.induction == induction)
        return &loop;
    return nullptr;
  }
  SmallVector<BoundedLoop, 0> loops;
  DominanceInfo dominance;
};

/// The element ordinals of `array` an index may select.
struct IndexOrdinals {
  SmallVector<unsigned, 4> ordinals;
  /// Each run of the selecting operation selects every one of them: the
  /// index is the induction of a bounded loop with constant bounds, the
  /// operation runs on every iteration, and no iteration leaves early.
  bool everyRun = false;
};

/// The element ordinals `index` may select in `array` (packed or unpacked)
/// for `user`: those a bounded loop's induction takes, or every element.
IndexOrdinals indexOrdinals(Value index, Type array, Operation *user,
                            const LoopFacts &facts) {
  IndexOrdinals result;
  unsigned count = sim::getAggregateNumElements(array);
  auto every = [&] {
    result.ordinals = llvm::to_vector<4>(llvm::seq(count));
    result.everyRun = false;
    return result;
  };
  while (Operation *cast = index.getDefiningOp()) {
    if (!isa<arith::ExtSIOp, arith::ExtUIOp>(cast))
      return every();
    index = cast->getOperand(0);
  }
  auto induction = dyn_cast<BlockArgument>(index);
  const BoundedLoop *loop = induction ? facts.lookup(induction) : nullptr;
  if (!loop || loop->initValue || loop->limitValue || !loop->isAdd ||
      loop->stride != 1 || !loop->members.contains(user->getBlock()))
    return every();
  auto continues = [&](const APInt &value) -> std::optional<bool> {
    switch (loop->predicate) {
    case arith::CmpIPredicate::slt:
      return value.slt(loop->limit);
    case arith::CmpIPredicate::sle:
      return value.sle(loop->limit);
    case arith::CmpIPredicate::ult:
      return value.ult(loop->limit);
    case arith::CmpIPredicate::ule:
      return value.ule(loop->limit);
    case arith::CmpIPredicate::ne:
      return value != loop->limit;
    default:
      return std::nullopt;
    }
  };
  llvm::SmallBitVector seen(count);
  APInt value = loop->init;
  for (unsigned step = 0;; ++step) {
    std::optional<bool> more = continues(value);
    if (!more)
      return every();
    if (!*more)
      break;
    // A loop visiting more values than there are elements reaches each.
    if (step > count)
      return every();
    std::optional<unsigned> ordinal = sim::getArrayElementOrdinal(
        array, loop->signedCompare
                   ? value.getSExtValue()
                   : static_cast<int64_t>(value.getZExtValue()));
    if (ordinal && *ordinal < count)
      seen.set(*ordinal);
    value += 1;
  }
  for (unsigned ordinal : seen.set_bits())
    result.ordinals.push_back(ordinal);
  bool exits = false;
  for (Block *block : loop->blocks)
    if (block != loop->header)
      for (Block *successor : block->getSuccessors())
        exits |= !loop->members.contains(successor);
  result.everyRun = !exits && facts.dominance.dominates(
                                  user->getBlock(), loop->latch->getBlock());
  return result;
}

/// The one indexed selection of a reference: an element of an array taken
/// at a dynamic index, with fixed selections around it. None when the
/// reference selects nothing dynamically or more than once, or picks a
/// fixed element of an unpacked array.
sim::SimRefArrayElementOp indexedSelection(Value reference) {
  sim::SimRefArrayElementOp found;
  while (Operation *view = reference.getDefiningOp()) {
    if (auto element = dyn_cast<sim::SimRefArrayElementOp>(view)) {
      if (found)
        return {};
      found = element;
    } else if (auto subelement = dyn_cast<sim::SimRefSubelementOp>(view)) {
      if (isa<sim::UnpackedArrayType>(
              cast<sim::RefType>(subelement.getInput().getType())
                  .getElementType()))
        return {};
    } else if (!isa<sim::SimRefExtractOp>(view)) {
      break;
    }
    reference = view->getOperand(0);
  }
  return found;
}

/// The may-known bits of one root or slot. A dense state holds every bit; a
/// shared state holds one element of an unpacked array, which every element
/// of the array shares.
class StorageBits final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;

  void shape(unsigned width, uint64_t stride, uint64_t count) {
    this->stride = stride;
    this->count = count;
    bits = covered = padding = APInt(width, 0);
    tracked = true;
  }
  /// Bits that belong to no value, such as alignment padding between array
  /// elements: they hold no X/Z.
  void setPadding(APInt bits) { padding = std::move(bits); }
  /// Start from `initial`, except the bits repeating writes cover.
  void start(const APInt &initial) { bits = initial & ~covered; }
  bool isTracked() const { return tracked; }
  bool isShared() const { return stride != 0; }
  unsigned getWidth() const { return bits.getBitWidth(); }

  /// The placement of an access of `width` bits at `low`; `lane`, for a
  /// dynamic access, holds its offset within an element when known.
  Placement place(uint64_t low, uint64_t width, bool dynamic,
                  std::optional<uint64_t> lane) const {
    Placement placement;
    if (!tracked || width == 0)
      return placement;
    if (!isShared()) {
      if (!dynamic && low <= getWidth() && width <= getWidth() - low) {
        placement.kind = Placement::Kind::Offset;
        placement.offset = static_cast<unsigned>(low);
      }
      return placement;
    }
    if (dynamic) {
      if (lane && *lane % stride + width <= stride) {
        placement.kind = Placement::Kind::Offset;
        placement.offset = static_cast<unsigned>(*lane % stride);
      }
      return placement;
    }
    // Whole elements, one or more, take an element placement whichever way
    // they are read, as an array value or as one packed element.
    if (low % stride == 0 && width % stride == 0 &&
        low / stride + width / stride <= count) {
      placement.kind = Placement::Kind::Elements;
      placement.elements = width / stride;
      placement.element = low / stride;
    } else if (low % stride + width <= stride) {
      placement.kind = Placement::Kind::Offset;
      placement.offset = static_cast<unsigned>(low % stride);
      placement.element = low / stride;
    }
    return placement;
  }

  /// The placement of an access of `width` bits at each of `lanes` of a
  /// dense state.
  Placement placeLanes(ArrayRef<uint64_t> lanes, uint64_t width) const {
    Placement placement;
    if (!tracked || isShared() || width == 0 || lanes.empty())
      return placement;
    for (uint64_t lane : lanes) {
      if (lane > getWidth() || width > getWidth() - lane)
        return Placement();
      placement.lanes.push_back(static_cast<unsigned>(lane));
    }
    placement.kind = Placement::Kind::Lanes;
    return placement;
  }

  /// Record that a repeating write overwrites the placed bits each time it
  /// runs: a fixed range of a dense state, or every element of a shared one.
  /// One element of a shared state, or an unknown location, leaves the other
  /// bits holding what they held, so their initial values stay.
  /// `everyElement` says the write reaches the placed bits of every element
  /// on each run, as a loop over all indices does.
  /// Fixed writes of single elements cover the bits they reach in all of
  /// them together (`finishCover`), as an unrolled loop over the elements
  /// does.
  void cover(const Placement &placement, unsigned width, bool everyElement) {
    // Each run of an indexed write reaches every lane its index takes.
    if (placement.kind == Placement::Kind::Lanes) {
      if (everyElement)
        covered |= reach(placement, width);
      return;
    }
    if ((placement.kind == Placement::Kind::Offset &&
         (!isShared() || everyElement)) ||
        (placement.kind == Placement::Kind::Elements &&
         (placement.elements == count || everyElement))) {
      covered |= reach(placement, width);
      return;
    }
    if (!isShared() || !placement.element ||
        placement.kind == Placement::Kind::Anywhere)
      return;
    uint64_t elements =
        placement.kind == Placement::Kind::Elements ? placement.elements : 1;
    for (uint64_t element = *placement.element;
         element != *placement.element + elements; ++element) {
      APInt &bits =
          perElement.try_emplace(element, APInt(getWidth(), 0)).first->second;
      bits |= reach(placement, width);
    }
  }
  void finishCover() {
    if (!isShared() || perElement.size() != count)
      return;
    APInt every = APInt::getAllOnes(getWidth());
    for (auto &entry : perElement)
      every &= entry.second;
    covered |= every;
    perElement.clear();
  }
  /// Add the may-known `mask` written at `placement`; a run-once write adds
  /// only bits no repeating write covers. An `elementwise` mask holds one
  /// element's bits, shared by every element the placement covers.
  ChangeResult add(const Placement &placement, const APInt &mask, bool runOnce,
                   bool elementwise = false) {
    if (!tracked)
      return ChangeResult::NoChange;
    APInt next = bits;
    switch (placement.kind) {
    case Placement::Kind::Offset:
      next |= mask.zextOrTrunc(getWidth()).shl(placement.offset) &
              reach(placement, mask.getBitWidth());
      break;
    case Placement::Kind::Lanes:
      for (unsigned lane : placement.lanes)
        next |= mask.zextOrTrunc(getWidth()).shl(lane) &
                APInt::getBitsSet(
                    getWidth(), lane,
                    std::min<uint64_t>(getWidth(), lane + mask.getBitWidth()));
      break;
    case Placement::Kind::Elements:
      if (elementwise) {
        next |= mask.zextOrTrunc(getWidth());
        break;
      }
      for (uint64_t element = 0; element != placement.elements; ++element)
        next |= mask.extractBits(getWidth(), element * stride)
                    .zextOrTrunc(getWidth());
      break;
    case Placement::Kind::Anywhere:
      next.setAllBits();
      break;
    }
    if (runOnce)
      next = bits | (next & ~covered);
    if (next == bits)
      return ChangeResult::NoChange;
    bits = std::move(next);
    return ChangeResult::Change;
  }
  /// The may-known bits a read of `width` bits at `placement` observes, or
  /// for an `elementwise` read, the bits every element it covers shares.
  APInt read(const Placement &placement, unsigned width,
             bool elementwise = false) const {
    if (elementwise)
      return placement.kind == Placement::Kind::Elements && width == getWidth()
                 ? bits
                 : APInt::getAllOnes(width);
    switch (placement.kind) {
    case Placement::Kind::Offset:
      if (placement.offset + width <= getWidth())
        return bits.extractBits(width, placement.offset);
      break;
    case Placement::Kind::Lanes: {
      APInt mask(width, 0);
      for (unsigned lane : placement.lanes) {
        if (lane + width > getWidth())
          return APInt::getAllOnes(width);
        mask |= bits.extractBits(width, lane);
      }
      return mask;
    }
    case Placement::Kind::Elements:
      if (placement.elements * stride == width) {
        APInt mask(width, 0);
        for (uint64_t element = 0; element != placement.elements; ++element)
          mask.insertBits(bits.extractBits(stride, 0), element * stride);
        return mask;
      }
      break;
    case Placement::Kind::Anywhere:
      break;
    }
    return APInt::getAllOnes(width);
  }
  /// The root bits that hold X/Z in steady state, given its initially known
  /// bits per state bit.
  SteadyUnknownAnalysis::Ranges unknown() const {
    APInt state = ~(bits | padding);
    SteadyUnknownAnalysis::Ranges ranges;
    for (uint64_t element = 0; element != (isShared() ? count : 1); ++element) {
      uint64_t base = element * stride;
      for (unsigned bit = 0; bit != getWidth();) {
        if (!state[bit]) {
          ++bit;
          continue;
        }
        unsigned end = bit;
        while (end != getWidth() && state[end])
          ++end;
        if (!ranges.empty() &&
            ranges.back().first + ranges.back().second == base + bit)
          ranges.back().second += end - bit;
        else
          ranges.push_back({base + bit, end - bit});
        bit = end;
      }
    }
    return ranges;
  }
  void print(raw_ostream &os) const override {
    if (tracked)
      bits.print(os, /*isSigned=*/false);
    else
      os << "untracked";
  }

private:
  /// The state bits an access of `width` bits at `placement` reaches.
  APInt reach(const Placement &placement, unsigned width) const {
    auto window = [&](unsigned offset) {
      return APInt::getBitsSet(
          getWidth(), offset,
          std::min<uint64_t>(getWidth(), uint64_t(offset) + width));
    };
    if (placement.kind == Placement::Kind::Offset)
      return window(placement.offset);
    if (placement.kind == Placement::Kind::Lanes) {
      APInt bits(getWidth(), 0);
      for (unsigned lane : placement.lanes)
        bits |= window(lane);
      return bits;
    }
    return APInt::getAllOnes(getWidth());
  }

  bool tracked = false;
  uint64_t stride = 0, count = 1;
  APInt bits, covered, padding;
  /// Bits fixed writes reach, by element (`cover`).
  llvm::DenseMap<uint64_t, APInt> perElement;
};

//===----------------------------------------------------------------------===//
// Shared inputs
//===----------------------------------------------------------------------===//

/// One state an access reaches, or none for storage the analysis does not
/// track.
struct Footprint {
  LatticeAnchor anchor;
  Placement placement;
};

struct Inputs {
  explicit Inputs(sim::SimDesignOp design) : handles(design) {}

  DataFlowSolver *solver = nullptr;
  HandleDataflowAnalysis handles;
  /// Reference facts and certificates of every function, and the private
  /// allocation each local reference addresses.
  DenseMap<Value, HandleFact> facts;
  DenseMap<Value, HandleCertificate> certificates;
  DenseMap<Value, Operation *> slots;
  /// Tracked roots by descriptor, with their initially known state bits.
  DenseMap<uint64_t, APInt> roots;
  /// Direct and task calls by callee.
  llvm::StringMap<SmallVector<Operation *, 2>> callers;
  /// Some write could not be attributed.
  bool top = false;
  /// Bounded loops by function, computed on demand.
  DenseMap<Operation *, std::unique_ptr<LoopFacts>> loops;

  const LoopFacts &loopsOf(sim::SimFuncOp function) {
    std::unique_ptr<LoopFacts> &facts = loops[function];
    if (!facts)
      facts = std::make_unique<LoopFacts>(function);
    return *facts;
  }

  /// The offsets of the bits an indexed selection `reference` of `width`
  /// bits may reach in its root: its lane in each element its index may
  /// select. None unless one dynamic element selection, at a known lane,
  /// forms the reference.
  std::optional<SmallVector<uint64_t, 4>>
  lanesOf(Value reference, const HandleFact &target,
          const HandleCertificate &certificate, uint64_t width) {
    sim::SimRefArrayElementOp element = indexedSelection(reference);
    auto function =
        element ? element->getParentOfType<sim::SimFuncOp>() : nullptr;
    if (!function || certificate.clipped || certificate.low < target.low)
      return std::nullopt;
    Type array =
        cast<sim::RefType>(element.getInput().getType()).getElementType();
    uint64_t lane = certificate.low - target.low;
    SmallVector<uint64_t, 4> lanes;
    for (unsigned ordinal :
         indexOrdinals(element.getIndex(), array, element, loopsOf(function))
             .ordinals) {
      auto selected = sim::getAggregateProvenanceSubelement(array, ordinal);
      if (!selected || lane > selected->second ||
          width > selected->second - lane)
        return std::nullopt;
      lanes.push_back(target.low + selected->first + lane);
    }
    return lanes;
  }

  static Value actual(Operation *call, unsigned index) {
    if (auto direct = dyn_cast<sim::SimCallOp>(call))
      return index < direct.getOperands().size() ? direct.getOperands()[index]
                                                 : Value();
    if (auto task = dyn_cast<sim::SimTaskCallOp>(call))
      return index < task.getArguments().size() ? task.getArguments()[index]
                                                : Value();
    return Value();
  }

  StorageBits *stateOf(LatticeAnchor anchor) {
    return solver->getOrCreateState<StorageBits>(anchor);
  }

  /// The footprints an access of `reference` may reach, following block
  /// arguments to incoming references and formals to the actuals of every
  /// call. A reference derived from a formal addresses `width` bits at
  /// `offset` within what the actual addresses. Fails when some target is
  /// unknown.
  bool resolve(Value reference, SmallVectorImpl<Footprint> &footprints,
               uint64_t offset = 0, uint64_t width = 0, bool dynamic = false,
               unsigned depth = 0) {
    if (depth == maximumResolutionDepth)
      return false;
    auto fact = facts.find(reference);
    if (auto argument = dyn_cast<BlockArgument>(reference);
        argument && !argument.getOwner()->isEntryBlock() &&
        (fact == facts.end() ||
         fact->second.resource == schedule::ComputeResourceKind::Unknown)) {
      Block *block = argument.getOwner();
      for (auto predecessor = block->pred_begin();
           predecessor != block->pred_end(); ++predecessor) {
        auto branch =
            dyn_cast<BranchOpInterface>((*predecessor)->getTerminator());
        if (!branch)
          return false;
        Value incoming = branch.getSuccessorOperands(
            predecessor.getSuccessorIndex())[argument.getArgNumber()];
        if (!incoming || incoming == reference ||
            !resolve(incoming, footprints, offset, width, dynamic, depth + 1))
          return false;
      }
      return true;
    }
    if (fact == facts.end())
      return false;
    const HandleFact &target = fact->second;
    uint64_t own = target.width ? target.width : target.rootWidth;
    // A dynamic selection may address any bit of its hull.
    uint64_t low = target.dynamic ? target.low : target.low + offset;
    if (!width || target.dynamic)
      width = own;
    dynamic |= target.dynamic;
    // A dynamic selection with a known lane addresses `width` bits at that
    // offset within some element.
    std::optional<uint64_t> lane;
    const HandleCertificate *certificate = nullptr;
    if (auto found = certificates.find(reference);
        depth == 0 && found != certificates.end() && found->second.laneKnown) {
      certificate = &found->second;
      lane = certificate->low;
      if (target.dynamic && certificate->width)
        width = certificate->width;
    }
    auto place = [&](LatticeAnchor anchor) {
      StorageBits *state = stateOf(anchor);
      Placement placement = state->place(low, width, dynamic, lane);
      // In a dense state, an indexed selection reaches its lane of each
      // element its index may select.
      if (placement.kind == Placement::Kind::Anywhere && target.dynamic &&
          certificate)
        if (auto lanes = lanesOf(reference, target, *certificate, width))
          placement = state->placeLanes(*lanes, width);
      footprints.push_back({anchor, std::move(placement)});
      return true;
    };
    if (Operation *allocation = slots.lookup(reference))
      return place(solver->getLatticeAnchor<SlotAnchor>(allocation));
    // Private storage the analysis does not track holds no design bits.
    if (target.resource == schedule::ComputeResourceKind::Local) {
      footprints.push_back({LatticeAnchor(), Placement()});
      return true;
    }
    if (target.resource == schedule::ComputeResourceKind::Storage &&
        target.descriptor) {
      if (!roots.contains(*target.descriptor)) {
        footprints.push_back({LatticeAnchor(), Placement()});
        return true;
      }
      return place(solver->getLatticeAnchor<RootAnchor>(*target.descriptor));
    }
    if (!target.formal)
      return false;
    sim::SimFuncOp function;
    if (auto argument = dyn_cast<BlockArgument>(reference))
      function = dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
    else if (Operation *producer = reference.getDefiningOp())
      function = producer->getParentOfType<sim::SimFuncOp>();
    auto sites = function ? callers.find(function.getSymName()) : callers.end();
    if (sites == callers.end() || sites->second.empty())
      return false;
    for (Operation *call : sites->second) {
      Value actualValue = actual(call, *target.formal);
      if (!actualValue ||
          !resolve(actualValue, footprints, low, width, dynamic, depth + 1))
        return false;
    }
    return true;
  }
};

/// The reference and stored value of each write the analysis models; any
/// other storage writer may store a known value through each reference it
/// writes.
std::pair<Value, Value> modeledWrite(Operation *op) {
  return llvm::TypeSwitch<Operation *, std::pair<Value, Value>>(op)
      .Case<sim::SimRefStoreOp, sim::SimRefStoreInertialPathOp>([](auto store) {
        return std::pair{store.getReference(), store.getValue()};
      })
      .Case<sim::SimRefAllocOp>([](auto allocation) {
        return std::pair{allocation.getResult(), allocation.getInitialValue()};
      })
      .Case<sim::SimNBAEnqueueOp>([](auto nba) {
        return isa<sim::RefType>(nba.getDestination().getType())
                   ? std::pair{nba.getDestination(), nba.getValue()}
                   : std::pair{Value(), Value()};
      })
      .Default([](Operation *) { return std::pair{Value(), Value()}; });
}

/// Whether `op` may write design storage the analysis must account for.
/// Calls to design functions write through their bodies, which are analyzed
/// themselves, and containers through the operations they hold.
bool isWrite(Operation *op) {
  if (isa<sim::SimRefStoreOp, sim::SimRefAllocOp, sim::SimNBAEnqueueOp,
          sim::SimRefStoreInertialPathOp>(op))
    return true;
  if (isa<sim::SimCallOp, sim::SimTaskCallOp>(op) || op->getNumRegions())
    return false;
  auto effects = dyn_cast<MemoryEffectOpInterface>(op);
  if (!effects)
    return !isMemoryEffectFree(op) && !op->hasTrait<OpTrait::IsTerminator>() &&
           !isa<RegionBranchOpInterface, CallOpInterface>(op);
  SmallVector<MemoryEffects::EffectInstance> instances;
  effects.getEffects(instances);
  return llvm::any_of(instances, [](const MemoryEffects::EffectInstance &e) {
    return isa<MemoryEffects::Write>(e.getEffect()) &&
           isa<sim::StorageResource>(e.getResource());
  });
}

//===----------------------------------------------------------------------===//
// Analyses
//===----------------------------------------------------------------------===//

/// May-known bits of values. Loads read the states the writes analysis below
/// grows.
class MayKnownValues final
    : public dataflow::SparseForwardDataFlowAnalysis<MayKnownLattice> {
public:
  MayKnownValues(DataFlowSolver &solver, Inputs &inputs)
      : SparseForwardDataFlowAnalysis(solver), inputs(inputs) {
    registerAnchorKind<RootAnchor>();
    registerAnchorKind<SlotAnchor>();
  }

  LogicalResult visitOperation(Operation *op,
                               ArrayRef<const MayKnownLattice *> operands,
                               ArrayRef<MayKnownLattice *> results) override {
    auto everything = [&] {
      for (MayKnownLattice *result : results)
        propagateIfChanged(result, result->join(MayKnownBits::all()));
      return success();
    };
    if (results.size() != 1)
      return everything();
    Value result = op->getResult(0);
    std::optional<unsigned> width = maskWidth(result.getType());
    if (!width || *width == 0 || *width > maximumTrackedBits)
      return everything();
    auto input = [&](unsigned index) -> std::optional<APInt> {
      const MayKnownBits &value = operands[index]->getValue();
      std::optional<unsigned> operandWidth =
          maskWidth(op->getOperand(index).getType());
      if (value.isBottom() || !operandWidth)
        return std::nullopt;
      return value.get(*operandWidth);
    };
    auto allOrNothing = [&](bool known) {
      return known ? APInt::getAllOnes(*width) : APInt(*width, 0);
    };
    auto put = [&](APInt mask) {
      propagateIfChanged(results[0],
                         results[0]->join(MayKnownBits::of(std::move(mask))));
      return success();
    };
    auto ready = [&] {
      return llvm::all_of(operands, [](const MayKnownLattice *operand) {
        return !operand->getValue().isBottom();
      });
    };
    // Unpacked array values of packed elements share one element mask;
    // packed aggregates place their subelements at their provenance offsets.
    bool array = isElementwise(result.getType());
    auto inputArray = [&](unsigned index) {
      return isElementwise(op->getOperand(index).getType());
    };
    // The union of `indices`' masks, each `*width` bits wide.
    auto unite = [&](ArrayRef<unsigned> indices) -> LogicalResult {
      APInt mask(*width, 0);
      for (unsigned index : indices) {
        std::optional<APInt> bits = input(index);
        if (!bits)
          return success();
        if (bits->getBitWidth() != *width)
          return everything();
        mask |= *bits;
      }
      return put(mask);
    };
    // Subelement `index` of packed `type` when it fits `width` bits.
    auto subelement = subelementPlacement;
    // `bits` placed at every element of packed array `type`.
    auto everyElement = [&](Type type,
                            const APInt &bits) -> std::optional<APInt> {
      APInt mask(*width, 0);
      for (unsigned index = 0, count = sim::getAggregateNumElements(type);
           index != count; ++index) {
        auto place = subelement(type, index, *width);
        if (!place || place->second != bits.getBitWidth())
          return std::nullopt;
        mask.insertBits(bits, place->first);
      }
      return mask;
    };
    return llvm::TypeSwitch<Operation *, LogicalResult>(op)
        .Case<sim::SimLogicConstantOp>([&](auto constant) {
          return put(~constant.getUnknown().zextOrTrunc(*width));
        })
        .Case<sim::SimAggregateDefaultOp>(
            [&](auto) { return put(defaultKnown(result.getType(), *width)); })
        .Case<sim::SimAggregateConstructOp>([&](auto) -> LogicalResult {
          if (array)
            return unite(llvm::to_vector(llvm::seq(op->getNumOperands())));
          if (!ready())
            return success();
          APInt mask(*width, 0);
          for (unsigned index = 0; index != op->getNumOperands(); ++index) {
            auto place = subelement(result.getType(), index, *width);
            std::optional<APInt> bits = input(index);
            if (!place || !bits || bits->getBitWidth() != place->second)
              return everything();
            mask.insertBits(*bits, place->first);
          }
          return put(mask);
        })
        .Case<sim::SimAggregateSplatOp>([&](auto) -> LogicalResult {
          if (array)
            return unite({0});
          std::optional<APInt> bits = input(0);
          if (!bits)
            return success();
          std::optional<APInt> mask = everyElement(result.getType(), *bits);
          return mask ? put(*mask) : everything();
        })
        .Case<sim::SimAggregateExtractOp>([&](auto extract) -> LogicalResult {
          if (inputArray(0))
            return unite({0});
          std::optional<APInt> bits = input(0);
          if (!bits)
            return success();
          auto place = subelement(extract.getInput().getType(),
                                  extract.getIndex(), bits->getBitWidth());
          if (!place || place->second != *width)
            return everything();
          return put(bits->extractBits(*width, place->first));
        })
        .Case<sim::SimAggregateInsertOp>([&](auto insert) -> LogicalResult {
          if (array)
            return unite({0, 1});
          if (!ready())
            return success();
          std::optional<APInt> base = input(0), replacement = input(1);
          auto place = subelement(result.getType(), insert.getIndex(), *width);
          if (!base || !replacement || base->getBitWidth() != *width ||
              !place || replacement->getBitWidth() != place->second)
            return everything();
          base->insertBits(*replacement, place->first);
          return put(*base);
        })
        .Case<sim::SimArrayDynInsertOp>([&](auto) -> LogicalResult {
          // An invalid index writes nothing (7.4.5); a valid one may replace
          // any element.
          if (array)
            return unite({0, 1});
          if (!ready())
            return success();
          std::optional<APInt> base = input(0), replacement = input(1);
          std::optional<APInt> spread =
              replacement ? everyElement(result.getType(), *replacement)
                          : std::nullopt;
          if (!base || base->getBitWidth() != *width || !spread)
            return everything();
          return put(*base | *spread);
        })
        .Case<sim::SimArrayDynExtractOp>([&](auto extract) -> LogicalResult {
          // Any element, or for an invalid index the element type's default.
          Type source = extract.getInput().getType();
          std::optional<APInt> bits = input(0);
          if (!bits)
            return success();
          APInt fallback = defaultKnown(result.getType(), *width);
          if (inputArray(0))
            return bits->getBitWidth() == *width ? put(*bits | fallback)
                                                 : everything();
          APInt mask = fallback;
          for (unsigned index = 0, count = sim::getAggregateNumElements(source);
               index != count; ++index) {
            auto place = subelement(source, index, bits->getBitWidth());
            if (!place || place->second != *width)
              return everything();
            mask |= bits->extractBits(*width, place->first);
          }
          return put(mask);
        })
        .Case<sim::SimRefLoadOp>([&](auto load) { return visitLoad(load); })
        // An X/Z condition keeps a bit only where both operands hold the
        // same known value (IEEE 1800-2023 11.4.11, Table 11-20).
        .Case<sim::SimLogicMuxOp>([&](auto) { return unite({1, 2}); })
        // Bitwise operators work per bit (11.4.8, Tables 11-11 to 11-15): a
        // known 0 decides an AND and a known 1 an OR, while XOR needs both
        // bits. An arithmetic result is all X for any X/Z operand bit
        // (11.4.3).
        .Case<sim::SimLogicBinaryOp>([&](auto binary) -> LogicalResult {
          if (!ready())
            return success();
          std::optional<APInt> lhs = input(0), rhs = input(1);
          if (!lhs || !rhs || lhs->getBitWidth() != *width ||
              rhs->getBitWidth() != *width)
            return everything();
          switch (binary.getKind()) {
          case sim::BinaryKind::And:
          case sim::BinaryKind::Or:
            return put(*lhs | *rhs);
          case sim::BinaryKind::Xor:
          case sim::BinaryKind::Xnor:
            return put(*lhs & *rhs);
          default:
            return put(allOrNothing(lhs->isAllOnes() && rhs->isAllOnes()));
          }
        })
        .Case<sim::SimLogicUnaryOp>([&](auto unary) -> LogicalResult {
          std::optional<APInt> bits = input(0);
          if (!bits)
            return success();
          switch (unary.getKind()) {
          case sim::UnaryKind::Plus:
          case sim::UnaryKind::BitNot:
            return bits->getBitWidth() == *width ? put(*bits) : everything();
          case sim::UnaryKind::LogicalNot:
            // One known 1 bit decides it (11.4.7).
            return put(allOrNothing(!bits->isZero()));
          case sim::UnaryKind::Negate:
            return put(allOrNothing(bits->isAllOnes()));
          }
          return everything();
        })
        .Case<sim::SimLogicReductionOp>([&](auto reduction) -> LogicalResult {
          // A known 0 decides an AND reduction and a known 1 an OR; XOR
          // needs every bit (11.4.9).
          std::optional<APInt> bits = input(0);
          if (!bits)
            return success();
          switch (reduction.getKind()) {
          case sim::ReductionKind::Xor:
          case sim::ReductionKind::Xnor:
            return put(allOrNothing(bits->isAllOnes()));
          default:
            return put(allOrNothing(!bits->isZero()));
          }
        })
        .Case<sim::SimLogicCompareOp>([&](auto compare) -> LogicalResult {
          if (!ready())
            return success();
          std::optional<APInt> lhs = input(0), rhs = input(1);
          if (!lhs || !rhs || lhs->getBitWidth() != rhs->getBitWidth())
            return everything();
          switch (compare.getKind()) {
          case sim::CompareKind::Eq:
          case sim::CompareKind::Ne:
            // Known only when every bit is known or two known bits differ
            // (11.4.5).
            return put(allOrNothing(!(*lhs & *rhs).isZero()));
          case sim::CompareKind::ULT:
          case sim::CompareKind::ULE:
          case sim::CompareKind::UGT:
          case sim::CompareKind::UGE:
          case sim::CompareKind::SLT:
          case sim::CompareKind::SLE:
          case sim::CompareKind::SGT:
          case sim::CompareKind::SGE:
            // Any X/Z operand bit makes a relation X (11.4.4).
            return put(allOrNothing(lhs->isAllOnes() && rhs->isAllOnes()));
          default:
            return everything();
          }
        })
        .Case<sim::SimLogicShiftOp>([&](auto shift) -> LogicalResult {
          // An X/Z amount makes the result X (11.4.10). A constant amount
          // moves the bits and fills vacated ones with known zeros, or
          // copies the sign for an arithmetic right shift.
          if (!ready())
            return success();
          std::optional<APInt> bits = input(0), amount = input(1);
          if (!bits || !amount || bits->getBitWidth() != *width)
            return everything();
          if (!amount->isAllOnes())
            return put(APInt(*width, 0));
          APInt by;
          auto constant =
              shift.getAmount()
                  .template getDefiningOp<sim::SimLogicConstantOp>();
          if (constant && constant.getUnknown().isZero())
            by = constant.getValue();
          else if (!matchPattern(shift.getAmount(), m_ConstantInt(&by)))
            return put(allOrNothing(!bits->isZero()));
          unsigned count = by.getActiveBits() > 32
                               ? *width
                               : std::min<uint64_t>(by.getZExtValue(), *width);
          switch (shift.getKind()) {
          case sim::ShiftKind::Left:
            return put(bits->shl(count) | APInt::getLowBitsSet(*width, count));
          case sim::ShiftKind::Right:
            return put(bits->lshr(count) |
                       APInt::getHighBitsSet(*width, count));
          case sim::ShiftKind::RightArith:
            return put(bits->ashr(count));
          }
          return everything();
        })
        .Case<sim::SimLogicDynExtractOp>([&](auto) -> LogicalResult {
          // An X/Z index makes every bit X (11.5.1).
          if (!ready())
            return success();
          std::optional<APInt> bits = input(0), index = input(1);
          if (!bits || !index)
            return everything();
          return put(allOrNothing(index->isAllOnes() && !bits->isZero()));
        })
        .Case<sim::SimLogicDynInsertOp>([&](auto) -> LogicalResult {
          // An X/Z or out-of-range index leaves the input unchanged.
          if (!ready())
            return success();
          std::optional<APInt> base = input(0), replacement = input(1);
          if (!base || !replacement || base->getBitWidth() != *width)
            return everything();
          return put(*base | allOrNothing(!replacement->isZero()));
        })
        .Case<sim::SimLogicReplicateOp>([&](auto) -> LogicalResult {
          std::optional<APInt> bits = input(0);
          if (!bits)
            return success();
          unsigned part = bits->getBitWidth();
          if (part == 0 || *width % part)
            return everything();
          APInt mask(*width, 0);
          for (unsigned at = 0; at != *width; at += part)
            mask.insertBits(*bits, at);
          return put(mask);
        })
        .Case<sim::SimLogicExtractOp>([&](auto extract) -> LogicalResult {
          std::optional<APInt> bits = input(0);
          if (!bits)
            return success();
          if (extract.getLowBit() + *width > bits->getBitWidth())
            return everything();
          return put(bits->extractBits(*width, extract.getLowBit()));
        })
        .Case<sim::SimLogicConcatOp>([&](auto) -> LogicalResult {
          if (!ready())
            return success();
          APInt mask(*width, 0);
          unsigned high = *width;
          for (unsigned index = 0; index != op->getNumOperands(); ++index) {
            std::optional<APInt> bits = input(index);
            if (!bits || bits->getBitWidth() > high)
              return everything();
            high -= bits->getBitWidth();
            mask.insertBits(*bits, high);
          }
          return put(mask);
        })
        .Case<sim::SimLogicInsertOp>([&](auto insert) -> LogicalResult {
          if (!ready())
            return success();
          std::optional<APInt> base = input(0), replacement = input(1);
          if (!base || !replacement || base->getBitWidth() != *width ||
              insert.getLowBit() + replacement->getBitWidth() > *width)
            return everything();
          base->insertBits(*replacement, insert.getLowBit());
          return put(*base);
        })
        .Case<sim::SimLogicResizeOp>([&](auto resize) -> LogicalResult {
          std::optional<APInt> bits = input(0);
          if (!bits)
            return success();
          unsigned from = bits->getBitWidth();
          if (*width <= from)
            return put(bits->trunc(*width));
          // Zero extension adds known bits; sign extension copies the sign.
          APInt mask = bits->zext(*width);
          if (resize.getIsSigned() ? (*bits)[from - 1] : true)
            mask.setBitsFrom(from);
          return put(mask);
        })
        .Case<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(
            [&](auto) -> LogicalResult {
              std::optional<APInt> bits = input(0);
              if (!bits)
                return success();
              return bits->getBitWidth() == *width ? put(*bits) : everything();
            })
        .Case<SelectLikeOpInterface>([&](auto select) -> LogicalResult {
          APInt mask(*width, 0);
          for (auto [index, value] : llvm::enumerate(op->getOperands())) {
            if (value == select.getCondition() ||
                (value != select.getTrueValue() &&
                 value != select.getFalseValue()))
              continue;
            std::optional<APInt> bits = input(index);
            if (!bits)
              return success();
            if (bits->getBitWidth() != *width)
              return everything();
            mask |= *bits;
          }
          return put(mask);
        })
        .Default([&](Operation *) { return everything(); });
  }

  void setToEntryState(MayKnownLattice *lattice) override {
    propagateIfChanged(lattice, lattice->join(MayKnownBits::all()));
  }

private:
  LogicalResult visitLoad(sim::SimRefLoadOp load) {
    MayKnownLattice *result = getLatticeElement(load.getResult());
    unsigned width = *maskWidth(load.getResult().getType());
    bool elementwise = isElementwise(load.getResult().getType());
    APInt mask(width, 0);
    SmallVector<Footprint, 2> footprints;
    // A read through a formal reaches whatever the callers pass; treat it as
    // known rather than follow every caller's state.
    auto fact = inputs.facts.find(load.getReference());
    if (fact == inputs.facts.end() || fact->second.formal ||
        !inputs.resolve(load.getReference(), footprints))
      mask.setAllBits();
    for (const Footprint &footprint : footprints) {
      if (!footprint.anchor) {
        mask.setAllBits();
        break;
      }
      const auto *state = getOrCreateFor<StorageBits>(
          getProgramPointAfter(load), footprint.anchor);
      mask |= state->read(footprint.placement, width, elementwise);
    }
    propagateIfChanged(result, result->join(MayKnownBits::of(mask)));
    return success();
  }

  Inputs &inputs;
};

/// Grows root and slot states by what each write may store. Visiting a write
/// subscribes it to the value it stores.
class MayKnownWrites final : public DataFlowAnalysis {
public:
  MayKnownWrites(DataFlowSolver &solver, Inputs &inputs)
      : DataFlowAnalysis(solver), inputs(inputs) {}

  LogicalResult initialize(Operation *top) override {
    top->walk([&](Operation *op) {
      if (isWrite(op))
        (void)visit(getProgramPointAfter(op));
    });
    return success();
  }

  LogicalResult visit(ProgramPoint *point) override {
    Operation *op = point->getPrevOp();
    if (!op)
      return success();
    // An allocation's initializer, like a root's initial value, starts only
    // the bits the slot's own writes do not replace.
    bool runOnce = runsOnce(op->getParentOfType<sim::SimFuncOp>()) ||
                   isa<sim::SimRefAllocOp>(op);
    auto write = [&](Value reference, std::optional<APInt> mayKnown,
                     bool elementwise = false) {
      SmallVector<Footprint, 2> footprints;
      if (!inputs.resolve(reference, footprints)) {
        inputs.top = true;
        return;
      }
      for (const Footprint &footprint : footprints) {
        if (!footprint.anchor)
          continue;
        StorageBits *state = inputs.stateOf(footprint.anchor);
        Placement placement = footprint.placement;
        // A value of unknown shape may make any bit it reaches known; an
        // array value's mask fits only whole elements of its own width.
        if (!mayKnown ||
            (elementwise ? placement.kind != Placement::Kind::Elements ||
                               mayKnown->getBitWidth() != state->getWidth()
                         : placement.kind == Placement::Kind::Elements &&
                               mayKnown->getBitWidth() !=
                                   placement.elements * state->getWidth()))
          placement.kind = Placement::Kind::Anywhere;
        propagateIfChanged(
            state,
            state->add(
                placement,
                mayKnown ? *mayKnown : APInt::getAllOnes(state->getWidth()),
                runOnce,
                elementwise && placement.kind == Placement::Kind::Elements));
      }
    };
    auto [reference, value] = modeledWrite(op);
    if (reference) {
      std::optional<unsigned> width = maskWidth(value.getType());
      if (!width || *width == 0 || *width > maximumTrackedBits)
        return write(reference, std::nullopt), success();
      const auto *lattice = getOrCreateFor<MayKnownLattice>(point, value);
      write(reference,
            lattice->getValue().isBottom() ? APInt(*width, 0)
                                           : lattice->getValue().get(*width),
            isElementwise(value.getType()));
      return success();
    }
    // Copies, overrides and any other storage writer: every bit of each
    // reference they write may become known.
    bool attributed = false;
    for (Value operand : op->getOperands())
      if (isa<sim::RefType>(operand.getType())) {
        write(operand, std::nullopt);
        attributed = true;
      }
    if (!attributed)
      inputs.top = true;
    return success();
  }

private:
  Inputs &inputs;
};

} // namespace

/// Shape `state` for storage of `type`: dense for a packed type, or one
/// state shared by the packed leaves of nested unpacked arrays while each
/// outer level lays its elements out back to back. Returns the bits its
/// default value holds known, or none when the type is not tracked.
static std::optional<APInt> shapeState(StorageBits *state, Type type) {
  std::optional<uint64_t> span = sim::getProvenanceSpan(type);
  if (!span)
    return std::nullopt;
  if (std::optional<unsigned> width = maskWidth(type);
      width && *width && *width <= maximumTrackedBits && *span == *width &&
      !isElementwise(type)) {
    state->shape(*width, 0, 1);
    state->setPadding(~getLogicalStorageMask(type).zextOrTrunc(*width));
    APInt unknown = hasMemberDefaults(type)
                        ? getDefaultStoragePlanes(type).unknown
                        : getDefaultUnknownMask(type);
    return ~unknown.zextOrTrunc(*width);
  }
  std::optional<uint64_t> stride;
  uint64_t count = 1;
  Type leaf = type;
  while (auto array = dyn_cast<sim::UnpackedArrayType>(leaf)) {
    stride = sim::getArrayElementStorageStride(array);
    leaf = array.getElementType();
    count *= sim::getAggregateNumElements(array);
    if (!stride || (isa<sim::UnpackedArrayType>(leaf) &&
                    sim::getProvenanceSpan(leaf) != stride)) {
      stride.reset();
      break;
    }
  }
  std::optional<unsigned> element = stride ? scalarWidth(leaf) : std::nullopt;
  if (!stride || !element || !*stride || *stride > maximumTrackedBits ||
      *element > *stride || count * *stride != *span)
    return std::nullopt;
  state->shape(static_cast<unsigned>(*stride), *stride, count);
  // Padding past a leaf's value holds no X/Z.
  APInt known = ~getDefaultUnknownMask(leaf).zextOrTrunc(*element).zext(
      static_cast<unsigned>(*stride));
  known.setBitsFrom(*element);
  state->setPadding(
      APInt::getBitsSetFrom(static_cast<unsigned>(*stride), *element));
  return known;
}

FailureOr<SteadyUnknownAnalysis>
SteadyUnknownAnalysis::compute(sim::SimDesignOp design) {
  SteadyUnknownAnalysis result;
  if (design.getBody().empty())
    return result;
  Inputs inputs(design);
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  inputs.solver = &solver;
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<MayKnownValues>(inputs);
  solver.load<MayKnownWrites>(inputs);

  // Four-state packed roots are dense; unpacked arrays of packed elements
  // share one element state. Their initially known bits come from the type's
  // default value (IEEE 1800-2023 6.8, Table 6-7, and 7.2.2).
  for (auto storage :
       design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
    if (!sim::containsFourStateLeaf(storage.getType()))
      continue;
    if (std::optional<APInt> known = shapeState(
            inputs.stateOf(LatticeAnchor(
                solver.getLatticeAnchor<RootAnchor>(storage.getId()))),
            storage.getType()))
      inputs.roots[storage.getId()] = std::move(*known);
  }

  for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
    if (function.isExternal())
      continue;
    HandleDataflowResult handles = inputs.handles.analyze(function);
    inputs.facts.insert(handles.facts.begin(), handles.facts.end());
    inputs.certificates.insert(handles.certificates.begin(),
                               handles.certificates.end());
    LocalStorageAnalysis locals(function);
    for (auto &[reference, fact] : handles.facts)
      if (sim::SimRefAllocOp allocation = locals.lookup(reference))
        inputs.slots[reference] = allocation;
    function.walk([&](Operation *op) {
      if (auto call = dyn_cast<sim::SimCallOp>(op))
        inputs.callers[call.getCallee()].push_back(op);
      else if (auto task = dyn_cast<sim::SimTaskCallOp>(op))
        inputs.callers[task.getCallee()].push_back(op);
      else if (auto allocation = dyn_cast<sim::SimRefAllocOp>(op))
        // A slot starts from its initializer, which its allocation writes.
        (void)shapeState(
            inputs.stateOf(solver.getLatticeAnchor<SlotAnchor>(op)),
            allocation.getType().getElementType());
    });
  }

  // The bits repeating writes reach take their steady state from those
  // writes; initialization only starts the others.
  // Writes to a private allocation cover its bits against its initializer
  // in whatever process runs them.
  SmallVector<LatticeAnchor> slotStates;
  design.walk([&](Operation *op) {
    auto function = op->getParentOfType<sim::SimFuncOp>();
    if (!function || !isWrite(op) || isa<sim::SimRefAllocOp>(op))
      return;
    bool roots = !runsOnce(function);
    SmallVector<Value, 2> references;
    if (Value reference = modeledWrite(op).first)
      references.push_back(reference);
    else
      for (Value operand : op->getOperands())
        if (isa<sim::RefType>(operand.getType()))
          references.push_back(operand);
    for (Value reference : references) {
      SmallVector<Footprint, 2> footprints;
      if (!inputs.resolve(reference, footprints))
        continue;
      std::optional<unsigned> width =
          maskWidth(cast<sim::RefType>(reference.getType()).getElementType());
      // An indexed write reaches every lane its index takes on each run when
      // the index is a loop's induction and the write runs on every
      // iteration; a shared state needs all of its elements taken.
      bool everyRun = false, everyElement = false;
      if (sim::SimRefArrayElementOp element = indexedSelection(reference)) {
        Type array =
            cast<sim::RefType>(element.getInput().getType()).getElementType();
        IndexOrdinals taken = indexOrdinals(element.getIndex(), array, op,
                                            inputs.loopsOf(function));
        everyRun = taken.everyRun;
        everyElement =
            everyRun && isElementwise(array) &&
            taken.ordinals.size() == sim::getAggregateNumElements(array);
      }
      for (const Footprint &footprint : footprints) {
        auto *anchor =
            llvm::dyn_cast_if_present<GenericLatticeAnchor *>(footprint.anchor);
        if (!anchor ||
            (isa<RootAnchor>(anchor) ? !roots : !isa<SlotAnchor>(anchor)))
          continue;
        if (isa<SlotAnchor>(anchor))
          slotStates.push_back(footprint.anchor);
        inputs.stateOf(footprint.anchor)
            ->cover(footprint.placement, width ? *width : 0,
                    footprint.placement.kind == Placement::Kind::Lanes
                        ? everyRun
                        : everyElement);
      }
    }
  });
  for (auto &[descriptor, initial] : inputs.roots) {
    StorageBits *state = inputs.stateOf(
        LatticeAnchor(solver.getLatticeAnchor<RootAnchor>(descriptor)));
    state->finishCover();
    state->start(initial);
  }
  for (LatticeAnchor slot : slotStates)
    inputs.stateOf(slot)->finishCover();

  if (failed(solver.initializeAndRun(design)))
    return failure();
  if (inputs.top) {
    result.top = true;
    return result;
  }
  for (auto &[descriptor, initial] : inputs.roots) {
    const auto *state = solver.lookupState<StorageBits>(
        LatticeAnchor(solver.getLatticeAnchor<RootAnchor>(descriptor)));
    Ranges ranges = state->unknown();
    if (!ranges.empty())
      result.roots[descriptor] = std::move(ranges);
  }
  return result;
}

} // namespace talos::analysis
