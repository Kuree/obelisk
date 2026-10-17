//===- AccessFootprintAnalysis.cpp - Reference footprints --------===//
#include "obelisk/Analysis/AccessFootprintAnalysis.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Analysis/Liveness.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/InferIntRangeInterface.h"
#include "mlir/Interfaces/Utils/InferIntRangeCommon.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"

#include <climits>
#include <memory>

using namespace mlir;
namespace obelisk::analysis {
namespace {

// Each environment denotes all executions satisfying its constant bindings.
// Missing bindings are unconstrained. Keeping a union of environments, rather
// than independent per-value sets, preserves relations such as i != j through
// nested loops and phi nodes. Reference bindings are root-relative offsets;
// identity and type bounds remain the responsibility of the sparse analysis.
using Environment = DenseMap<Value, APInt>;

/// One environment of the union. Environments are immutable and shared by
/// every program point a transfer leaves them unchanged at. The signature
/// sets one bit per binding, so a subset's bits are a subset of its
/// superset's: most failing subsumption checks need no lookup.
struct Path {
  std::shared_ptr<const Environment> bindings;
  uint64_t signature = 0;
  /// When the lattice holding this copy received it; see `unseen`.
  uint64_t sequence = 0;

  explicit Path(Environment environment)
      : bindings(std::make_shared<const Environment>(std::move(environment))) {
    for (const auto &[value, constant] : *bindings)
      signature |= bit(value, constant);
  }
  const Environment &operator*() const { return *bindings; }
  const Environment *operator->() const { return bindings.get(); }
  static uint64_t bit(Value value, const APInt &constant) {
    return uint64_t{1} << (llvm::hash_combine(value.getAsOpaquePointer(),
                                              llvm::hash_value(constant)) &
                           63);
  }
};

/// Whether `a` denotes every execution `b` does: `b` has all of its bindings.
bool subsumes(const Path &a, const Path &b) {
  if (a.bindings == b.bindings)
    return true;
  if (a->size() > b->size() || (a.signature & ~b.signature))
    return false;
  return llvm::all_of(*a, [&](const auto &entry) {
    auto found = b->find(entry.first);
    return found != b->end() && found->second == entry.second;
  });
}

/// The bindings of `common` that `path` also has.
Environment intersect(const Environment &common, const Environment &path) {
  Environment result;
  for (const auto &entry : common) {
    auto found = path.find(entry.first);
    if (found != path.end() && found->second == entry.second)
      result.insert(entry);
  }
  return result;
}

class FootprintLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  SmallVector<Path, 1> paths;

  /// The paths of `source` whose transfers this lattice has not received.
  /// A lattice only appends, erases, or (widened) renews its paths, so these
  /// are a suffix. A transfer this lattice received stays subsumed here, also
  /// once it widens, so transferring an earlier path again changes nothing.
  ArrayRef<Path> unseen(const FootprintLattice &source) {
    uint64_t &seen = received[&source];
    const Path *first = llvm::find_if(
        source.paths, [&](const Path &path) { return path.sequence > seen; });
    if (!source.paths.empty())
      seen = source.paths.back().sequence;
    return ArrayRef<Path>(first, source.paths.end());
  }

  ChangeResult merge(const Path &incoming) {
    if (widened) {
      Environment common = intersect(*paths.front(), *incoming);
      if (common.size() == paths.front()->size())
        return ChangeResult::NoChange;
      paths.front() = Path(std::move(common));
      paths.front().sequence = ++sequence;
      return ChangeResult::Change;
    }
    if (llvm::any_of(
            paths, [&](const Path &path) { return subsumes(path, incoming); }))
      return ChangeResult::NoChange;
    llvm::erase_if(paths,
                   [&](const Path &path) { return subsumes(incoming, path); });
    paths.push_back(incoming);
    paths.back().sequence = ++sequence;
    // Bound the disjunctive domain, including unbounded/wrapping loops. After
    // widening, retain only bindings common to every path; later joins can
    // only remove them. No iteration cap is used to certify a partial result.
    if (paths.size() > 256) {
      Environment common = *paths.front();
      for (const Path &path : llvm::drop_begin(paths))
        common = intersect(common, *path);
      paths.clear();
      paths.push_back(Path(std::move(common)));
      paths.back().sequence = ++sequence;
      widened = true;
    }
    return ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &other) override {
    ChangeResult changed = ChangeResult::NoChange;
    for (const Path &path : static_cast<const FootprintLattice &>(other).paths)
      changed |= merge(path);
    return changed;
  }
  void print(raw_ostream &os) const override { os << paths.size() << " paths"; }

private:
  bool widened = false;
  uint64_t sequence = 0;
  DenseMap<const FootprintLattice *, uint64_t> received;
};

class FootprintDataflow final
    : public dataflow::DenseForwardDataFlowAnalysis<FootprintLattice> {
public:
  FootprintDataflow(DataFlowSolver &solver, sim::SimFuncOp function,
                    const HandleDataflowResult &handles,
                    ArrayRef<std::pair<Operation *, Value>> accesses)
      : DenseForwardDataFlowAnalysis(solver), handles(handles),
        liveness(function) {
    SmallVector<Value> pending;
    for (auto [access, reference] : accesses)
      pending.push_back(reference);
    function.walk([&](cf::CondBranchOp branch) {
      pending.push_back(branch.getCondition());
    });
    // Build a backward SSA slice only. The MLIR solver handles cycles and
    // predecessor propagation; this walk never evaluates a predecessor.
    while (!pending.empty()) {
      Value value = pending.pop_back_val();
      auto width = sim::getPackedWidth(value.getType());
      if (!isa<sim::RefType>(value.getType()) &&
          (!isa<IntegerType, sim::LogicType>(value.getType()) || !width ||
           *width > 128))
        continue;
      if (!relevant.insert(value).second)
        continue;
      if (auto argument = dyn_cast<BlockArgument>(value)) {
        for (Block *pred : argument.getOwner()->getPredecessors())
          if (auto branch = dyn_cast<BranchOpInterface>(pred->getTerminator()))
            for (unsigned i = 0; i < pred->getNumSuccessors(); ++i)
              if (pred->getSuccessor(i) == argument.getOwner()) {
                auto operands = branch.getSuccessorOperands(i);
                if (argument.getArgNumber() >=
                    operands.getProducedOperandCount())
                  pending.push_back(operands[argument.getArgNumber()]);
              }
      } else if (Operation *op = value.getDefiningOp()) {
        if (isa<InferIntRangeInterface, sim::SimLogicFromBitsOp,
                sim::SimLogicToBitsOp, sim::SimLogicIsTrueOp,
                sim::SimLogicCompareOp, sim::SimLogicResizeOp,
                sim::SimLogicBinaryOp, sim::SimLogicExtractOp,
                sim::SimLogicUnaryOp, sim::SimLogicReductionOp,
                sim::SimLogicConcatOp, sim::SimLogicReplicateOp,
                sim::SimLogicMuxOp, sim::SimLogicShiftOp,
                sim::SimRefArrayElementOp, sim::SimRefSubelementOp,
                sim::SimRefExtractOp, sim::SimRefDynExtractOp>(op))
          llvm::append_range(pending, op->getOperands());
      }
    }
    // A binding is live until its last use in the block unless it is live
    // out; it enters at its definition or, for a live-in value or block
    // argument, at the block entry. So the bindings that die after an
    // operation are among its operands and results, plus the arguments of
    // its block at the first operation.
    for (Block &block : function.getBody())
      for (Operation &op : block) {
        if (op.hasTrait<OpTrait::IsTerminator>())
          continue;
        SmallVector<Value, 2> &values = dyingAfter[&op];
        auto consider = [&](Value value) {
          if (relevant.contains(value) && !llvm::is_contained(values, value) &&
              liveness.isDeadAfter(value, &op))
            values.push_back(value);
        };
        if (&op == &block.front())
          for (BlockArgument argument : block.getArguments())
            consider(argument);
        for (Value value : op.getOperands())
          consider(value);
        for (Value value : op.getResults())
          consider(value);
        if (values.empty())
          dyingAfter.erase(&op);
      }
  }

  std::optional<APInt> get(Value value, const Environment &path) const {
    auto found = path.find(value);
    if (found != path.end())
      return found->second;
    APInt constant;
    if (matchPattern(value, m_ConstantInt(&constant)))
      return constant;
    if (auto logic = value.getDefiningOp<sim::SimLogicConstantOp>())
      if (logic.getUnknown().isZero())
        return logic.getValue();
    if (isa<sim::RefType>(value.getType())) {
      auto fact = handles.facts.find(value);
      auto proof = handles.certificates.find(value);
      auto width = sim::getProvenanceSpan(value.getType());
      if (fact != handles.facts.end() && proof != handles.certificates.end() &&
          proof->second.inBounds && !fact->second.dynamic && width &&
          fact->second.width == *width)
        return APInt(64, fact->second.low);
    }
    return std::nullopt;
  }

  LogicalResult visitOperation(Operation *op, const FootprintLattice &before,
                               FootprintLattice *after) override {
    // Discard dead bindings to avoid correlating unrelated loop lifetimes.
    // Edge transfer still needs the terminator's condition and phi operands.
    ArrayRef<Value> dying;
    if (!op->hasTrait<OpTrait::IsTerminator>())
      if (auto found = dyingAfter.find(op); found != dyingAfter.end())
        dying = found->second;
    bool binds = llvm::any_of(op->getResults(), [&](Value value) {
      return relevant.contains(value);
    });
    for (const Path &path : after->unseen(before)) {
      if (!binds && llvm::none_of(op->getResults(),
                                  [&](Value value) {
                                    return path->contains(value);
                                  }) &&
          llvm::none_of(dying,
                        [&](Value value) { return path->contains(value); })) {
        propagateIfChanged(after, after->merge(path));
        continue;
      }
      Environment next = *path;
      for (Value result : op->getResults())
        next.erase(result);
      auto put = [&](Value value, const APInt &constant) {
        if (relevant.contains(value))
          next.insert_or_assign(value, constant);
      };
      if (auto interface = dyn_cast<InferIntRangeInterface>(op);
          interface && binds) {
        // An unknown logic operand may be X, so it cannot be modeled as an
        // arbitrary integer of the right width for a logic result.
        SmallVector<ConstantIntRanges> operands;
        bool unknownLogic = false;
        for (Value value : op->getOperands()) {
          auto constant = get(value, *path);
          unknownLogic |= !constant && isa<sim::LogicType>(value.getType());
          unsigned width =
              ConstantIntRanges::getStorageBitwidth(value.getType());
          if (auto packed = sim::getPackedWidth(value.getType()))
            width = *packed;
          operands.push_back(constant ? ConstantIntRanges::constant(*constant)
                                      : ConstantIntRanges::maxRange(width));
        }
        interface.inferResultRanges(
            operands, [&](Value value, const ConstantIntRanges &range) {
              if (range.umin() == range.umax() &&
                  !(unknownLogic && isa<sim::LogicType>(value.getType())))
                put(value, range.umin());
            });
      } else if (op->getNumResults() == 1 &&
                 relevant.contains(op->getResult(0))) {
        Value result = op->getResult(0);
        if (isa<sim::RefType>(result.getType())) {
          if (auto offset = projectReference(op, *path))
            put(result, APInt(64, *offset));
        } else if (auto constant = evaluateLogic(op, *path)) {
          put(result, *constant);
        }
      }
      for (Value value : dying)
        next.erase(value);
      propagateIfChanged(after, after->merge(Path(std::move(next))));
    }
    return success();
  }

  void visitBlockTransfer(Block *block, ProgramPoint *, Block *predecessor,
                          const FootprintLattice &before,
                          FootprintLattice *after) override {
    Operation *terminator = predecessor->getTerminator();
    auto branch = dyn_cast<BranchOpInterface>(terminator);
    auto conditional = dyn_cast<cf::CondBranchOp>(terminator);
    const auto &liveIn = liveness.getLiveIn(block);
    for (const Path &path : after->unseen(before))
      for (unsigned edge = 0; edge < terminator->getNumSuccessors(); ++edge) {
        if (terminator->getSuccessor(edge) != block)
          continue;
        if (conditional)
          if (auto condition = get(conditional.getCondition(), *path))
            if (edge != (condition->isZero() ? 1u : 0u))
              continue;
        // Parallel phi assignment reads the predecessor environment. Handle
        // both edges to the same successor separately, including phi swaps.
        SmallVector<std::pair<Value, APInt>> phis;
        if (branch) {
          auto operands = branch.getSuccessorOperands(edge);
          for (BlockArgument argument : block->getArguments())
            if (relevant.contains(argument) &&
                argument.getArgNumber() >= operands.getProducedOperandCount())
              if (auto value = get(operands[argument.getArgNumber()], *path))
                phis.emplace_back(argument, *value);
        }
        if (phis.empty() && llvm::all_of(*path, [&](const auto &entry) {
              return liveIn.contains(entry.first);
            })) {
          propagateIfChanged(after, after->merge(path));
          continue;
        }
        Environment next;
        for (const auto &entry : *path)
          if (liveIn.contains(entry.first))
            next.insert(entry);
        for (auto &[argument, value] : phis)
          next.insert_or_assign(argument, value);
        propagateIfChanged(after, after->merge(Path(std::move(next))));
      }
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const FootprintLattice &before,
                                    FootprintLattice *after) override {
    // Callees cannot change SSA snapshots. Results remain unconstrained.
    (void)visitOperation(call, before, after);
  }

private:
  void setToEntryState(FootprintLattice *state) override {
    propagateIfChanged(state, state->merge(Path(Environment{})));
  }

  std::optional<uint64_t> projectReference(Operation *op,
                                           const Environment &path) const {
    if (!isa<sim::SimRefArrayElementOp, sim::SimRefSubelementOp,
             sim::SimRefExtractOp, sim::SimRefDynExtractOp>(op))
      return std::nullopt;
    Value parent = op->getOperand(0), result = op->getResult(0);
    auto base = get(parent, path);
    auto parentWidth = sim::getProvenanceSpan(parent.getType());
    auto width = sim::getProvenanceSpan(result.getType());
    if (!base || !parentWidth || !width)
      return std::nullopt;
    Type type = cast<sim::RefType>(parent.getType()).getElementType();
    uint64_t low = 0;
    if (auto field = dyn_cast<sim::SimRefSubelementOp>(op)) {
      for (int64_t index : field.getIndices()) {
        if (index < 0 || uint64_t(index) > UINT_MAX)
          return std::nullopt;
        auto child = sim::getAggregateProvenanceSubelement(type, index);
        if (!child || child->first > UINT64_MAX - low)
          return std::nullopt;
        low += child->first;
        type = sim::getAggregateElementType(type, index);
      }
    } else if (auto extract = dyn_cast<sim::SimRefExtractOp>(op)) {
      low = extract.getLowBit();
    } else {
      auto index = get(op->getOperand(1), path);
      if (!index || !index->isSignedIntN(64))
        return std::nullopt;
      if (isa<sim::SimRefArrayElementOp>(op)) {
        auto ordinal = sim::getArrayElementOrdinal(type, index->getSExtValue());
        auto child = ordinal
                         ? sim::getAggregateProvenanceSubelement(type, *ordinal)
                         : std::nullopt;
        if (!child)
          return std::nullopt;
        low = child->first;
      } else {
        if (index->isNegative())
          return std::nullopt;
        low = index->getZExtValue();
      }
    }
    if (low > *parentWidth || *width > *parentWidth - low ||
        low > UINT64_MAX - base->getZExtValue())
      return std::nullopt;
    return base->getZExtValue() + low;
  }

  /// Evaluate a logic operation whose operands are known two-state bits.
  /// Every canonical spelling of an operation must be covered, since
  /// canonicalization may choose any of them (for example a truncation is a
  /// low slice and `x != 0` is a truth value).
  std::optional<APInt> evaluateLogic(Operation *op,
                                     const Environment &path) const {
    if (op->getNumOperands() == 0)
      return std::nullopt;
    if (auto concat = dyn_cast<sim::SimLogicConcatOp>(op)) {
      unsigned width = concat.getType().getWidth();
      APInt result = APInt::getZero(width);
      unsigned trailing = width;
      for (Value input : concat.getInputs()) {
        auto bits = get(input, path);
        if (!bits)
          return std::nullopt;
        trailing -= bits->getBitWidth();
        result |= bits->zext(width).shl(trailing);
      }
      return result;
    }
    if (auto mux = dyn_cast<sim::SimLogicMuxOp>(op)) {
      auto condition = get(mux.getCondition(), path);
      if (!condition)
        return std::nullopt;
      return get(condition->isZero() ? mux.getFalseValue() : mux.getTrueValue(),
                 path);
    }
    auto lhs = get(op->getOperand(0), path);
    if (!lhs)
      return std::nullopt;
    if (isa<sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp>(op))
      return lhs;
    if (isa<sim::SimLogicIsTrueOp>(op))
      return APInt(1, !lhs->isZero());
    if (auto resize = dyn_cast<sim::SimLogicResizeOp>(op)) {
      auto width = sim::getPackedWidth(resize.getType());
      if (width)
        return resize.getIsSigned() ? lhs->sextOrTrunc(*width)
                                    : lhs->zextOrTrunc(*width);
    }
    if (auto extract = dyn_cast<sim::SimLogicExtractOp>(op))
      return lhs->lshr(extract.getLowBit()).trunc(extract.getType().getWidth());
    if (auto replicate = dyn_cast<sim::SimLogicReplicateOp>(op)) {
      unsigned width = replicate.getType().getWidth();
      APInt result = APInt::getZero(width);
      for (int64_t copy = 0; copy < replicate.getCount(); ++copy)
        result |= lhs->zext(width).shl(copy * lhs->getBitWidth());
      return result;
    }
    if (auto unary = dyn_cast<sim::SimLogicUnaryOp>(op)) {
      switch (unary.getKind()) {
      case sim::UnaryKind::Plus:
        return lhs;
      case sim::UnaryKind::Negate:
        return -*lhs;
      case sim::UnaryKind::BitNot:
        return ~*lhs;
      case sim::UnaryKind::LogicalNot:
        return APInt(1, lhs->isZero());
      }
    }
    if (auto reduction = dyn_cast<sim::SimLogicReductionOp>(op)) {
      bool value = false;
      switch (reduction.getKind()) {
      case sim::ReductionKind::And:
      case sim::ReductionKind::Nand:
        value = lhs->isAllOnes();
        break;
      case sim::ReductionKind::Or:
      case sim::ReductionKind::Nor:
        value = !lhs->isZero();
        break;
      case sim::ReductionKind::Xor:
      case sim::ReductionKind::Xnor:
        value = lhs->popcount() & 1;
        break;
      }
      bool inverted = reduction.getKind() == sim::ReductionKind::Nand ||
                      reduction.getKind() == sim::ReductionKind::Nor ||
                      reduction.getKind() == sim::ReductionKind::Xnor;
      return APInt(1, value != inverted);
    }
    if (auto shift = dyn_cast<sim::SimLogicShiftOp>(op)) {
      // IEEE 1800-2023 11.4.10: the amount is unsigned and may have any width.
      auto amount = get(shift.getAmount(), path);
      if (!amount)
        return std::nullopt;
      unsigned width = lhs->getBitWidth();
      bool vacated = amount->uge(width);
      uint64_t count = vacated ? width : amount->getZExtValue();
      switch (shift.getKind()) {
      case sim::ShiftKind::Left:
        return vacated ? APInt::getZero(width) : lhs->shl(count);
      case sim::ShiftKind::Right:
        return vacated ? APInt::getZero(width) : lhs->lshr(count);
      case sim::ShiftKind::RightArith:
        return lhs->ashr(std::min<uint64_t>(count, width - 1));
      }
    }
    if (op->getNumOperands() != 2)
      return std::nullopt;
    auto rhs = get(op->getOperand(1), path);
    if (!rhs || lhs->getBitWidth() != rhs->getBitWidth())
      return std::nullopt;
    if (auto compare = dyn_cast<sim::SimLogicCompareOp>(op)) {
      bool value;
      switch (compare.getKind()) {
      case sim::CompareKind::Eq:
      case sim::CompareKind::CaseEq:
        value = *lhs == *rhs;
        break;
      case sim::CompareKind::Ne:
      case sim::CompareKind::CaseNe:
        value = *lhs != *rhs;
        break;
      case sim::CompareKind::SLT:
        value = lhs->slt(*rhs);
        break;
      case sim::CompareKind::SLE:
        value = lhs->sle(*rhs);
        break;
      case sim::CompareKind::SGT:
        value = lhs->sgt(*rhs);
        break;
      case sim::CompareKind::SGE:
        value = lhs->sge(*rhs);
        break;
      case sim::CompareKind::ULT:
        value = lhs->ult(*rhs);
        break;
      case sim::CompareKind::ULE:
        value = lhs->ule(*rhs);
        break;
      case sim::CompareKind::UGT:
        value = lhs->ugt(*rhs);
        break;
      case sim::CompareKind::UGE:
        value = lhs->uge(*rhs);
        break;
      default:
        return std::nullopt;
      }
      return APInt(*sim::getPackedWidth(compare.getType()), value);
    }
    if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op)) {
      switch (binary.getKind()) {
      case sim::BinaryKind::Add:
        return *lhs + *rhs;
      case sim::BinaryKind::Sub:
        return *lhs - *rhs;
      case sim::BinaryKind::Mul:
        return *lhs * *rhs;
      case sim::BinaryKind::And:
        return *lhs & *rhs;
      case sim::BinaryKind::Or:
        return *lhs | *rhs;
      case sim::BinaryKind::Xor:
        return *lhs ^ *rhs;
      default:
        break;
      }
    }
    return std::nullopt;
  }

  const HandleDataflowResult &handles;
  Liveness liveness;
  DenseSet<Value> relevant;
  /// Relevant values that are dead after each non-terminator operation.
  DenseMap<Operation *, SmallVector<Value, 2>> dyingAfter;
};
} // namespace

AccessFootprintAnalysis::AccessFootprintAnalysis(
    sim::SimFuncOp function, const HandleDataflowResult &handles) {
  if (function.isExternal() || function.getBody().empty())
    return;
  SmallVector<std::pair<Operation *, Value>> accesses;
  // Activation bodies have a flat CFG. Decline region/suspension semantics
  // until their entry and lifetime transfers have dedicated certificates.
  for (Block &block : function.getBody()) {
    if (!isa<cf::BranchOp, cf::CondBranchOp, sim::SimReturnOp>(
            block.getTerminator()))
      return;
    for (Operation &op : block) {
      if (op.getNumRegions())
        return;
      if (isa<sim::SimRefArrayElementOp, sim::SimRefSubelementOp,
              sim::SimRefExtractOp, sim::SimRefDynExtractOp>(op))
        continue;
      for (Value value : op.getOperands()) {
        if (!isa<sim::RefType>(value.getType()))
          continue;
        auto fact = handles.facts.find(value);
        auto width = sim::getProvenanceSpan(value.getType());
        if (fact != handles.facts.end() && fact->second.descriptor && width &&
            (fact->second.dynamic || fact->second.width != *width))
          accesses.emplace_back(&op, value);
      }
    }
  }
  if (accesses.empty())
    return;
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  auto *analysis = solver.load<FootprintDataflow>(function, handles, accesses);
  if (failed(solver.initializeAndRun(function)))
    return;
  for (auto [access, reference] : accesses) {
    auto *state = solver.lookupState<FootprintLattice>(
        solver.getProgramPointBefore(access));
    if (!state || state->paths.empty())
      continue;
    const auto &root = handles.facts.find(reference)->second;
    uint64_t width = *sim::getProvenanceSpan(reference.getType());
    Footprint ranges;
    bool known = true;
    for (const auto &path : state->paths) {
      auto low = analysis->get(reference, *path);
      if (!low || low->getZExtValue() > root.rootWidth ||
          width > root.rootWidth - low->getZExtValue()) {
        known = false;
        break;
      }
      ranges.emplace_back(low->getZExtValue(), width);
    }
    if (!known)
      continue;
    llvm::sort(ranges);
    Footprint merged;
    for (auto [low, width] : ranges) {
      if (!merged.empty() && low <= merged.back().first + merged.back().second)
        merged.back().second =
            std::max(merged.back().first + merged.back().second, low + width) -
            merged.back().first;
      else
        merged.emplace_back(low, width);
    }
    footprints.try_emplace(std::pair{access, reference}, std::move(merged));
  }
}

const AccessFootprintAnalysis::Footprint *
AccessFootprintAnalysis::lookup(Operation *access, Value reference) const {
  auto found = footprints.find({access, reference});
  return found == footprints.end() ? nullptr : &found->second;
}
} // namespace obelisk::analysis
