#include "obelisk/Analysis/NativeWordAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Analysis/ScopedIntegerRangeAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk::analysis {
bool isWordReferenceIdentity(Operation *op) {
  if (!isa<sim::SimRefExtractOp, sim::SimRefSubelementOp>(op))
    return false;
  Type input = cast<sim::RefType>(op->getOperand(0).getType()).getElementType();
  Type result = cast<sim::RefType>(op->getResult(0).getType()).getElementType();
  auto width = sim::getPackedWidth(input);
  if (!width || width != sim::getPackedWidth(result) ||
      sim::getPackedScalarType(input) != sim::getPackedScalarType(result))
    return false;
  if (auto extract = dyn_cast<sim::SimRefExtractOp>(op))
    return extract.getLowBit() == 0;
  for (int64_t index : cast<sim::SimRefSubelementOp>(op).getIndices()) {
    auto element = sim::getAggregateProvenanceSubelement(input, index);
    if (!element || element->first != 0)
      return false;
    input = sim::getAggregateElementType(input, index);
  }
  return true;
}
uint64_t getWordLaneStorageWidth(Value value) {
  Operation *op = value.getDefiningOp();
  while (op && isWordReferenceIdentity(op)) {
    value = op->getOperand(0);
    op = value.getDefiningOp();
  }
  if (isa_and_nonnull<sim::SimRefArrayElementOp, sim::SimArrayDynExtractOp>(op)) {
    Type parent = op->getOperand(0).getType();
    if (auto reference = dyn_cast<sim::RefType>(parent))
      parent = reference.getElementType();
    return sim::getArrayElementStorageStride(parent).value_or(0);
  }
  Type type = value.getType();
  if (auto reference = dyn_cast<sim::RefType>(type))
    type = reference.getElementType();
  return sim::getPackedWidth(type).value_or(0);
}
bool isWordBitwiseComputation(Operation *op) {
  if (isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(op))
    return true;
  if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op))
    return binary.getKind() == sim::BinaryKind::And ||
           binary.getKind() == sim::BinaryKind::Or ||
           binary.getKind() == sim::BinaryKind::Xor ||
           binary.getKind() == sim::BinaryKind::Xnor;
  if (auto unary = dyn_cast<sim::SimLogicUnaryOp>(op))
    return unary.getKind() == sim::UnaryKind::Plus || unary.isBitwiseNegation();
  return isa<arith::AndIOp, arith::OrIOp, arith::XOrIOp>(op);
}
namespace {
// Only these values can be queried by the loop-local lane solvers. External
// incoming phi operands are included even when no operation in the loop uses
// them directly. Seeding every value in a fused function for every loop would
// turn independent loop queries into quadratic work.
void forEachLoopValue(const BoundedLoop &loop,
                      function_ref<void(Value)> callback) {
  DenseSet<Value> seen;
  auto visit = [&](Value value) {
    if (seen.insert(value).second)
      callback(value);
  };
  for (Block *block : loop.blocks) {
    for (Value argument : block->getArguments())
      visit(argument);
    for (Operation &op : *block) {
      for (Value value : op.getOperands())
        visit(value);
      for (Value value : op.getResults())
        visit(value);
    }
    if (block->getNumArguments())
      for (Block *predecessor : block->getPredecessors())
        if (auto branch =
                dyn_cast<BranchOpInterface>(predecessor->getTerminator()))
          for (unsigned edge = 0; edge < predecessor->getNumSuccessors();
               ++edge)
            if (predecessor->getSuccessor(edge) == block)
              for (Value value :
                   branch.getSuccessorOperands(edge).getForwardedOperands())
                visit(value);
  }
}

using Kind = WordLaneKind;
bool isIndex(Kind kind) {
  return kind == Kind::Index || kind == Kind::ReverseIndex;
}
Kind reverseIndex(Kind kind) {
  return kind == Kind::Index ? Kind::ReverseIndex : Kind::Index;
}
struct LaneFact {
  bool initialized = false;
  Kind kind = Kind::Unknown;
  LaneFact() = default;
  LaneFact(Kind kind) : initialized(true), kind(kind) {}
  bool operator==(const LaneFact &other) const {
    return initialized == other.initialized && kind == other.kind;
  }
  static LaneFact join(const LaneFact &a, const LaneFact &b) {
    if (!a.initialized)
      return b;
    if (!b.initialized)
      return a;
    return a.kind == b.kind ? a : LaneFact(Kind::Unknown);
  }
  void print(raw_ostream &os) const {
    os << initialized << ':' << unsigned(kind);
  }
};
using Lattice = dataflow::Lattice<LaneFact>;

// An affine address residue (scale * outer-IV + offset) modulo a power of
// two covering all outer lanes. Unknown inner values multiplied by that
// modulus vanish. Distinct outer residues therefore cannot alias, including
// different executions of the same store in nested loops. This is a sparse
// congruence domain, separate from lane uniformity and interval containment.
struct AddressResidue {
  bool initialized = false, known = false, constantOffset = false;
  bool invariant = false;
  uint64_t scale = 0, offset = 0;
  static AddressResidue unknown() { return {true, false, false, false, 0, 0}; }
  static AddressResidue affine(uint64_t scale, uint64_t offset,
                               bool constantOffset = true,
                               bool invariant = false) {
    return {true, true, constantOffset, invariant, scale, offset};
  }
  bool operator==(const AddressResidue &other) const {
    return initialized == other.initialized && known == other.known &&
           constantOffset == other.constantOffset &&
           invariant == other.invariant && scale == other.scale &&
           offset == other.offset;
  }
  static AddressResidue join(const AddressResidue &a, const AddressResidue &b) {
    if (!a.initialized)
      return b;
    if (!b.initialized)
      return a;
    return a == b ? a : unknown();
  }
  void print(raw_ostream &os) const {
    os << initialized << ':' << known << ':' << constantOffset << ':'
       << invariant << ':' << scale << ':' << offset;
  }
};
using AddressLattice = dataflow::Lattice<AddressResidue>;
class AddressCongruence final : public DataFlowAnalysis {
public:
  AddressCongruence(DataFlowSolver &solver, const BoundedLoop &loop,
                    unsigned bits)
      : DataFlowAnalysis(solver), loop(loop), bits(bits),
        mask(APInt::getLowBitsSet(64, bits).getZExtValue()) {}
  AddressResidue constant(Value value) {
    APInt number;
    if (matchPattern(value, m_ConstantInt(&number)))
      return AddressResidue::affine(
          0, number.sextOrTrunc(64).getZExtValue() & mask, true, true);
    if (auto logic = value.getDefiningOp<sim::SimLogicConstantOp>())
      if (logic.getUnknown().isZero())
        return AddressResidue::affine(
            0, logic.getValue().sextOrTrunc(64).getZExtValue() & mask, true,
            true);
    return AddressResidue::unknown();
  }
  LogicalResult initialize(Operation *) override {
    auto seed = [&](Value value) {
      bool outside = !loop.members.contains(value.getParentBlock());
      if (!outside && !isa<BlockArgument>(value))
        return;
      AddressResidue residue = constant(value);
      if (value == loop.induction)
        residue = AddressResidue::affine(1, 0);
      bool captured = false;
      if (auto argument = dyn_cast<BlockArgument>(value))
        captured = argument.getOwner() == loop.header &&
                   argument != loop.induction &&
                   loop.latch->getOperand(argument.getArgNumber()) == argument;
      if ((outside || captured) && !residue.known)
        // Retain an external SSA offset symbolically: its value is fixed
        // throughout this loop, including unchanged header captures, even
        // when its residue is not a constant.
        residue = AddressResidue::affine(0, 0, false, true);
      auto *state = getOrCreate<AddressLattice>(value);
      propagateIfChanged(state, state->join(residue));
    };
    forEachLoopValue(loop, seed);
    for (Block *block : loop.blocks)
      for (Operation &op : *block)
        if (failed(visit(getProgramPointAfter(&op))))
          return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    Operation *op = point->getPrevOp();
    if (!op || op->getNumResults() != 1)
      return success();
    SmallVector<AddressResidue> inputs;
    for (Value value : op->getOperands()) {
      auto input = getOrCreateFor<AddressLattice>(point, value)->getValue();
      if (!input.initialized)
        return success();
      inputs.push_back(input);
    }
    auto add = [&](AddressResidue a, AddressResidue b, uint64_t factor = 1) {
      factor &= mask;
      if (!factor)
        return a;
      if (!a.known || !b.known)
        return AddressResidue::unknown();
      bool constant = a.constantOffset && b.constantOffset;
      return AddressResidue::affine(
          (a.scale + factor * b.scale) & mask,
          constant ? (a.offset + factor * b.offset) & mask : 0, constant);
    };
    AddressResidue result = constant(op->getResult(0));
    auto width = sim::getPackedWidth(op->getResult(0).getType());
    if (isa<arith::AddIOp, arith::SubIOp>(op) && width && *width >= bits)
      result = add(inputs[0], inputs[1], isa<arith::SubIOp>(op) ? -1 : 1);
    else if (isa<arith::MulIOp>(op) && width && *width >= bits) {
      for (unsigned i = 0; i != 2; ++i)
        if (inputs[i].known && inputs[i].constantOffset &&
            inputs[i].scale == 0) {
          uint64_t factor = inputs[i].offset;
          auto other = inputs[1 - i];
          if (factor == 0 || other.known)
            result = AddressResidue::affine(
                (factor * other.scale) & mask, (factor * other.offset) & mask,
                factor == 0 || other.constantOffset);
        }
    } else if (isa<arith::ExtSIOp, arith::ExtUIOp, arith::TruncIOp,
                   sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp>(op)) {
      auto inputWidth = sim::getPackedWidth(op->getOperand(0).getType());
      if (width && inputWidth && *width >= bits && *inputWidth >= bits)
        result = inputs[0];
    } else if (isWordReferenceIdentity(op)) {
      result = inputs[0];
    } else if (auto select = dyn_cast<sim::SimRefArrayElementOp>(op)) {
      Type parent = select.getInput().getType().getElementType();
      int64_t base;
      bool reverse;
      if (auto array = dyn_cast<sim::PackedArrayType>(parent)) {
        base = array.getRight();
        reverse = array.getLeft() < array.getRight();
      } else {
        auto unpacked = cast<sim::UnpackedArrayType>(parent);
        base = unpacked.getLeft();
        reverse = unpacked.getLeft() > unpacked.getRight();
      }
      auto span = sim::getArrayElementStorageStride(parent);
      if (span) {
        auto index =
            add(inputs[1], AddressResidue::affine(0, -uint64_t(base) & mask));
        result = add(inputs[0], index, reverse ? -*span : *span);
      }
    } else if (isa<sim::SimRefDynExtractOp>(op)) {
      result = add(inputs[0], inputs[1]);
    } else if (auto extract = dyn_cast<sim::SimRefExtractOp>(op)) {
      result =
          add(inputs[0], AddressResidue::affine(0, extract.getLowBit() & mask));
    } else if (auto select = dyn_cast<sim::SimRefSubelementOp>(op)) {
      Type type = select.getInput().getType().getElementType();
      result = inputs[0];
      for (int64_t index : select.getIndices()) {
        auto element = sim::getAggregateProvenanceSubelement(type, index);
        if (!element) {
          result = AddressResidue::unknown();
          break;
        }
        result = add(result, AddressResidue::affine(0, element->first & mask));
        type = sim::getAggregateElementType(type, index);
      }
    }
    // Track full-value invariance separately from its address residue. For
    // example, inner-IV * 8 has residue zero modulo 8 but is not invariant;
    // dividing it by 8 must not manufacture an invariant symbolic offset.
    // Pure context lookups and fixed projections of invariant values, however,
    // are invariant even when rematerialized inside the loop by capture
    // specialization. Their physical base need not be a literal integer.
    bool invariant = !op->getNumRegions() && !op->getNumSuccessors() &&
                     isMemoryEffectFree(op) &&
                     llvm::all_of(inputs, [](const AddressResidue &input) {
                       return input.invariant;
                     });
    if (invariant && !result.known)
      result = AddressResidue::affine(0, 0, false);
    result.invariant = invariant;
    auto *state = getOrCreate<AddressLattice>(op->getResult(0));
    propagateIfChanged(state, state->join(result));
    return success();
  }

private:
  const BoundedLoop &loop;
  unsigned bits;
  uint64_t mask;
};

/// Sparse SSA dependencies (including phi edges), combined with dense scoped
/// integer bounds. No instruction-count cutoff or recursive use-def guess is
/// a proof of independence, and a CFG join cannot improve an unknown fact.
class LaneDataflow final : public DataFlowAnalysis {
public:
  LaneDataflow(DataFlowSolver &solver, const BoundedLoop &loop,
               NativeWordRangeAnalysis &ranges)
      : DataFlowAnalysis(solver), loop(loop), ranges(ranges) {}
  LogicalResult initialize(Operation *) override {
    auto seed = [&](Value value) {
      Block *block = value.getParentBlock();
      if (value == loop.induction || !loop.members.contains(block)) {
        auto *state = getOrCreate<Lattice>(value);
        propagateIfChanged(state, state->join(LaneFact(value == loop.induction
                                                           ? Kind::Index
                                                           : Kind::Uniform)));
      }
    };
    forEachLoopValue(loop, seed);
    for (Block *block : loop.blocks) {
      if (failed(visit(getProgramPointBefore(block))))
        return failure();
      for (Operation &op : *block)
        if (failed(visit(getProgramPointAfter(&op))))
          return failure();
    }
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    auto fact = [&](Value value) {
      return getOrCreateFor<Lattice>(point, value)->getValue();
    };
    auto publish = [&](Value value, LaneFact next) {
      auto *state = getOrCreate<Lattice>(value);
      propagateIfChanged(state, state->join(next));
    };
    if (point->isBlockStart()) {
      Block *block = point->getBlock();
      for (BlockArgument argument : block->getArguments()) {
        if (argument == loop.induction)
          continue;
        LaneFact next;
        for (Block *pred : block->getPredecessors()) {
          auto branch = dyn_cast<BranchOpInterface>(pred->getTerminator());
          if (!branch) {
            next = LaneFact(Kind::Unknown);
            break;
          }
          for (unsigned i = 0; i < pred->getNumSuccessors(); ++i)
            if (pred->getSuccessor(i) == block) {
              auto operands = branch.getSuccessorOperands(i);
              if (argument.getArgNumber() < operands.getProducedOperandCount())
                next = LaneFact(Kind::Unknown);
              else
                next = LaneFact::join(next,
                                      fact(operands[argument.getArgNumber()]));
            }
        }
        publish(argument, next);
      }
      return success();
    }
    Operation *op = point->getPrevOp();
    if (!op || !op->getNumResults())
      return success();
    SmallVector<Kind> inputs;
    for (Value value : op->getOperands()) {
      LaneFact input = fact(value);
      if (!input.initialized)
        return success();
      inputs.push_back(input.kind);
    }
    Kind result = Kind::Unknown;
    if (llvm::all_of(inputs, [](Kind kind) { return kind == Kind::Uniform; })) {
      result = Kind::Uniform;
    } else if (op->getNumResults() == 1) {
      if (isa<arith::AddIOp, arith::SubIOp>(op) && inputs.size() == 2 &&
          ((isIndex(inputs[0]) && inputs[1] == Kind::Uniform) ||
           (isIndex(inputs[1]) && inputs[0] == Kind::Uniform))) {
        // Unit-stride geometry must hold in mathematical integers, not just
        // modulo the index type. Wrapping indices can remain in array bounds.
        if (!ranges.isReachable(op))
          return success();
        auto a = ranges.getRange(op, op->getOperand(0));
        auto b = ranges.getRange(op, op->getOperand(1));
        if (a && b) {
          unsigned width = a->smax().getBitWidth();
          bool subtract = isa<arith::SubIOp>(op);
          APInt maximum =
              subtract ? a->smax().sext(width + 1) - b->smin().sext(width + 1)
                       : a->smax().sext(width + 1) + b->smax().sext(width + 1);
          APInt minimum =
              subtract ? a->smin().sext(width + 1) - b->smax().sext(width + 1)
                       : a->smin().sext(width + 1) + b->smin().sext(width + 1);
          if (maximum.sle(APInt::getSignedMaxValue(width).sext(width + 1)) &&
              minimum.sge(APInt::getSignedMinValue(width).sext(width + 1)))
            result = isIndex(inputs[0]) ? inputs[0]
                     : subtract         ? reverseIndex(inputs[1])
                                        : inputs[1];
        }
      } else if (isa<arith::ExtSIOp, sim::SimLogicFromBitsOp,
                     sim::SimLogicToBitsOp>(op) &&
                 isIndex(inputs.front())) {
        result = inputs.front();
      } else if (isa<arith::ExtUIOp>(op) && isIndex(inputs.front())) {
        if (!ranges.isReachable(op))
          return success();
        auto range = ranges.getRange(op, op->getOperand(0));
        // Zero extension preserves affine order on either side of the signed
        // boundary, but not across it (e.g. -1,0 becomes 255,0).
        if (range && range->smin().isNegative() == range->smax().isNegative())
          result = inputs.front();
      } else if (isWordReferenceIdentity(op) &&
                 (inputs[0] == Kind::Reference ||
                  inputs[0] == Kind::ReverseReference)) {
        result = inputs[0];
      } else if (auto select = dyn_cast<sim::SimRefArrayElementOp>(op)) {
        Type parent =
            cast<sim::RefType>(select.getInput().getType()).getElementType();
        Type element;
        bool descendingStorage = false;
        if (auto array = dyn_cast<sim::PackedArrayType>(parent)) {
          element = array.getElementType();
          descendingStorage = array.getLeft() < array.getRight();
        } else if (auto array = dyn_cast<sim::UnpackedArrayType>(parent)) {
          element = array.getElementType();
          descendingStorage = array.getLeft() > array.getRight();
        }
        auto width = element ? sim::getPackedWidth(element) : std::nullopt;
        // The word rewriter retains this physical lane stride and clears ABI
        // padding on stores. Lane values still have their source bit widths.
        auto stride = sim::getArrayElementStorageStride(parent);
        if (width && stride && *stride >= *width && *width > 0 &&
            *stride <= 32 && inputs[0] == Kind::Uniform && isIndex(inputs[1])) {
          bool reversed =
              (inputs[1] == Kind::ReverseIndex) != descendingStorage;
          result = reversed ? Kind::ReverseReference : Kind::Reference;
        }
      } else if (auto select = dyn_cast<sim::SimRefDynExtractOp>(op)) {
        if (inputs[0] == Kind::Uniform && isIndex(inputs[1]) &&
            isWordLaneType(cast<sim::RefType>(select.getResult().getType())
                               .getElementType()))
          result = inputs[1] == Kind::Index ? Kind::Reference
                                            : Kind::ReverseReference;
      } else if (auto select = dyn_cast<sim::SimArrayDynExtractOp>(op)) {
        auto array =
            dyn_cast<sim::PackedArrayType>(select.getInput().getType());
        auto unpacked =
            dyn_cast<sim::UnpackedArrayType>(select.getInput().getType());
        auto width = sim::getPackedWidth(select.getResult().getType());
        auto stride =
            sim::getArrayElementStorageStride(select.getInput().getType());
        if ((array || unpacked) && width && stride && *stride >= *width &&
            *width > 0 && *stride <= 32 && inputs[0] == Kind::Uniform &&
            isIndex(inputs[1])) {
          if (!ranges.isReachable(op))
            return success();
          auto range = ranges.getRange(op, select.getIndex());
          if (range) {
            unsigned bits = std::max(65u, range->smin().getBitWidth());
            int64_t left = array ? array.getLeft() : unpacked.getLeft();
            int64_t right = array ? array.getRight() : unpacked.getRight();
            APInt low(bits, std::min(left, right), true);
            APInt high(bits, std::max(left, right), true);
            if (range->smin().sextOrTrunc(bits).sge(low) &&
                range->smax().sextOrTrunc(bits).sle(high))
              result = Kind::Logic;
          }
        }
      } else if (isa<sim::SimLogicDynExtractOp, sim::SimBitsDynExtractOp>(op) &&
                 isWordLaneType(op->getResult(0).getType()) &&
                 inputs[0] == Kind::Uniform && isIndex(inputs[1])) {
        if (!ranges.isReachable(op))
          return success();
        auto range = ranges.getRange(op, op->getOperand(1));
        auto width = sim::getPackedWidth(op->getOperand(0).getType());
        if (range && width && !range->smin().isNegative() &&
            range->umax().ult(*width))
          result = Kind::Logic;
      } else if (isa<sim::SimRefLoadOp>(op) &&
                 (inputs[0] == Kind::Reference ||
                  inputs[0] == Kind::ReverseReference)) {
        result = Kind::Logic;
      } else if (isWordLaneComputation(op) ||
                 (sim::getPackedWidth(op->getResult(0).getType()).value_or(0) >
                      0 &&
                  sim::getPackedWidth(op->getResult(0).getType())
                          .value_or(33) <= 32 &&
                  isWordBitwiseComputation(op))) {
        if (llvm::all_of(inputs, [](Kind k) {
              return k == Kind::Logic || k == Kind::Uniform;
            }))
          result = Kind::Logic;
      }
    }
    for (Value value : op->getResults())
      publish(value, LaneFact(result));
    return success();
  }

private:
  const BoundedLoop &loop;
  NativeWordRangeAnalysis &ranges;
};
} // namespace

struct NativeWordRangeAnalysis::Impl {
  DataFlowSolver solver{DataFlowConfig().setInterprocedural(false)};
  ScopedIntegerRangeAnalysis *ranges;
  bool valid;
  explicit Impl(sim::SimFuncOp function) {
    solver.load<dataflow::DeadCodeAnalysis>();
    solver.load<dataflow::SparseConstantPropagation>();
    ranges = solver.load<ScopedIntegerRangeAnalysis>(function.getOperation());
    valid = succeeded(solver.initializeAndRun(function));
  }
  const ScopedRangeLattice *lookup(Operation *op) {
    return valid ? solver.lookupState<ScopedRangeLattice>(
                       solver.getProgramPointBefore(op))
                 : nullptr;
  }
};
NativeWordRangeAnalysis::NativeWordRangeAnalysis(sim::SimFuncOp function)
    : impl(std::make_unique<Impl>(function)) {}
NativeWordRangeAnalysis::~NativeWordRangeAnalysis() = default;
bool NativeWordRangeAnalysis::isReachable(Operation *op) {
  auto *scope = impl->lookup(op);
  return scope && scope->reachable;
}
std::optional<ConstantIntRanges>
NativeWordRangeAnalysis::getRange(Operation *op, Value value) {
  auto *scope = impl->lookup(op);
  return scope && scope->reachable ? impl->ranges->getRange(value, *scope)
                                   : std::nullopt;
}

NativeWordAnalysis::NativeWordAnalysis(sim::SimFuncOp function,
                                       const BoundedLoop &loop,
                                       NativeWordRangeAnalysis &ranges,
                                       unsigned elementWidth) {
  // Native eval functions are independent pass-manager work items. Never
  // inspect a callee body while another worker may be rewriting it; calls
  // remain explicit eligibility barriers and supply no lane/range proof.
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<LaneDataflow>(loop, ranges);
  unsigned residueBits = 0;
  if (llvm::any_of(loop.blocks, [&](Block *block) {
        return block != loop.header && block->getNumArguments() != 0;
      })) {
    if (auto bounds = getInductionRange(loop, false)) {
      unsigned width = bounds->smax().getBitWidth() + 8;
      APInt span = bounds->smax().sext(width) - bounds->smin().sext(width) + 1;
      residueBits = (span * elementWidth - 1).getActiveBits();
      if (residueBits > 0 && residueBits < 64)
        solver.load<AddressCongruence>(loop, residueBits);
    }
  }
  if (failed(solver.initializeAndRun(function)))
    return;
  auto record = [&](Value value) {
    auto *state = solver.lookupState<Lattice>(value);
    if (state && state->getValue().initialized)
      kinds[value] = state->getValue().kind;
    if (auto *address = solver.lookupState<AddressLattice>(value)) {
      const auto &fact = address->getValue();
      uint64_t mask = APInt::getLowBitsSet(64, residueBits).getZExtValue();
      auto reference = dyn_cast<sim::RefType>(value.getType());
      auto width = reference ? sim::getPackedWidth(reference.getElementType())
                             : std::nullopt;
      uint64_t stride = reference ? getWordLaneStorageWidth(value) : 0;
      if (width && *width && stride >= *width && fact.known &&
          (fact.scale == stride || fact.scale == (-stride & mask)))
        disjointIterations.insert(value);
    }
  };
  forEachLoopValue(loop, record);
}
} // namespace obelisk::analysis
