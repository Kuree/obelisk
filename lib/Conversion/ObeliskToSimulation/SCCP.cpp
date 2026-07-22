//===- SCCP.cpp - Threaded interprocedural simulation SCCP ---------------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/FoldUtils.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

using namespace mlir;
using namespace mlir::dataflow;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMSCCPPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

namespace sim = ::obelisk::sim;

using BoundaryFact = ConstantValue;

/// SCCP's uninitialized value is the optimistic bottom boundary fact and its
/// initialized null attribute is the pessimistic unknown fact. Keeping MLIR's
/// lattice value intact also preserves the dialect that must materialize an
/// exact constant.
static BoundaryFact getUnknownFact() {
  return BoundaryFact::getUnknownConstant();
}

static bool mergeFact(BoundaryFact &destination,
                      const BoundaryFact &contribution) {
  BoundaryFact joined = BoundaryFact::join(destination, contribution);
  if (joined == destination)
    return false;
  destination = joined;
  return true;
}

/// A non-interprocedural SCCP analysis whose only deviation from MLIR's stock
/// transfer functions is the boundary state supplied by the design scheduler.
class BoundarySparseConstantPropagation : public SparseConstantPropagation {
public:
  BoundarySparseConstantPropagation(
      DataFlowSolver &solver,
      const DenseMap<Value, BoundaryFact> &boundarySeeds)
      : SparseConstantPropagation(solver), boundarySeeds(boundarySeeds) {}

  void setToEntryState(Lattice<ConstantValue> *lattice) override {
    auto found = boundarySeeds.find(lattice->getAnchor());
    if (found == boundarySeeds.end()) {
      SparseConstantPropagation::setToEntryState(lattice);
      return;
    }

    // An uninitialized seed deliberately remains at bottom. The design-level
    // fixed point will either discover a contribution or turn it into unknown
    // before the final rewriting solve.
    if (found->second.isUninitialized())
      return;
    propagateIfChanged(lattice, lattice->join(found->second));
  }

private:
  const DenseMap<Value, BoundaryFact> &boundarySeeds;
};

enum class BoundarySiteKind { Call, Spawn, Return };

struct BoundarySite {
  Operation *operation = nullptr;
  BoundarySiteKind kind = BoundarySiteKind::Return;
  std::optional<unsigned> callee;
};

struct FunctionInfo {
  sim::SimFuncOp function;
  std::string symbol;
  unsigned irOrder = 0;
  SmallVector<BoundaryFact> arguments;
  SmallVector<BoundaryFact> results;
  SmallVector<BoundarySite> sites;
  SmallVector<unsigned> callers;
};

struct SiteObservation {
  bool executable = false;
  SmallVector<BoundaryFact> operands;
};

struct FunctionObservation {
  SmallVector<SiteObservation> sites;
};

static bool isExecutable(DataFlowSolver &solver, Operation *operation) {
  const auto *state = solver.lookupState<Executable>(
      solver.getProgramPointBefore(operation->getBlock()));
  return state && state->isLive();
}

static BoundaryFact getFact(DataFlowSolver &solver, Value value) {
  const auto *lattice = solver.lookupState<Lattice<ConstantValue>>(value);
  if (!lattice)
    return BoundaryFact::getUninitialized();
  return lattice->getValue();
}

static void addBoundarySeeds(ArrayRef<FunctionInfo> functions,
                             unsigned functionIndex,
                             DenseMap<Value, BoundaryFact> &seeds) {
  const FunctionInfo &info = functions[functionIndex];
  sim::SimFuncOp function = info.function;
  for (auto [argument, fact] :
       llvm::zip_equal(function.getArguments(), info.arguments))
    seeds.try_emplace(argument, fact);

  for (const BoundarySite &site : info.sites) {
    if (auto call = dyn_cast<sim::SimCallOp>(site.operation)) {
      if (!site.callee) {
        for (Value result : call.getResults())
          seeds.try_emplace(result, getUnknownFact());
        continue;
      }
      for (auto [result, fact] :
           llvm::zip_equal(call.getResults(), functions[*site.callee].results))
        seeds.try_emplace(result, fact);
      continue;
    }
    if (auto spawn = dyn_cast<sim::SimSpawnOp>(site.operation))
      seeds.try_emplace(spawn.getProcess(), getUnknownFact());
  }
}

static LogicalResult analyzeFunction(ArrayRef<FunctionInfo> functions,
                                     unsigned functionIndex,
                                     FunctionObservation &observation) {
  const FunctionInfo &info = functions[functionIndex];
  sim::SimFuncOp function = info.function;
  if (function.isExternal())
    return success();

  DenseMap<Value, BoundaryFact> seeds;
  addBoundarySeeds(functions, functionIndex, seeds);

  DataFlowConfig config;
  config.setInterprocedural(false);
  DataFlowSolver solver(config);
  solver.load<DeadCodeAnalysis>();
  solver.load<BoundarySparseConstantPropagation>(seeds);
  if (failed(solver.initializeAndRun(function)))
    return failure();

  observation.sites.resize(info.sites.size());
  for (auto [site, result] : llvm::zip(info.sites, observation.sites)) {
    if (!isExecutable(solver, site.operation))
      continue;
    result.executable = true;
    if (auto call = dyn_cast<sim::SimCallOp>(site.operation)) {
      for (Value operand : call.getOperands())
        result.operands.push_back(getFact(solver, operand));
    } else if (auto spawn = dyn_cast<sim::SimSpawnOp>(site.operation)) {
      for (Value operand : spawn.getOperands())
        result.operands.push_back(getFact(solver, operand));
    } else {
      auto returnOp = cast<sim::SimReturnOp>(site.operation);
      for (Value operand : returnOp.getOperands())
        result.operands.push_back(getFact(solver, operand));
    }
  }
  return success();
}

/// Replace constants and newly dead operations using the same rewrite order as
/// MLIR's SCCP pass. Each invocation owns its builder and folder and touches
/// only one isolated function body.
static void rewriteFunction(DataFlowSolver &solver, sim::SimFuncOp function) {
  SmallVector<Block *> worklist;
  auto addToWorklist = [&](MutableArrayRef<Region> regions) {
    for (Region &region : regions)
      for (Block &block : llvm::reverse(region))
        worklist.push_back(&block);
  };

  OperationFolder folder(function.getContext());
  OpBuilder builder(function.getContext());
  addToWorklist(function->getRegions());
  while (!worklist.empty()) {
    Block *block = worklist.pop_back_val();
    for (Operation &operation : llvm::make_early_inc_range(*block)) {
      builder.setInsertionPoint(&operation);
      bool replacedAll = operation.getNumResults() != 0;
      for (Value result : operation.getResults()) {
        const auto *lattice =
            solver.lookupState<Lattice<ConstantValue>>(result);
        if (!lattice || lattice->getValue().isUninitialized() ||
            !lattice->getValue().getConstantValue()) {
          replacedAll = false;
          continue;
        }
        const ConstantValue &value = lattice->getValue();
        Value constant = folder.getOrCreateConstant(
            builder.getInsertionBlock(), value.getConstantDialect(),
            value.getConstantValue(), result.getType());
        if (!constant) {
          replacedAll = false;
          continue;
        }
        result.replaceAllUsesWith(constant);
      }
      if (replacedAll && wouldOpBeTriviallyDead(&operation)) {
        operation.erase();
        continue;
      }
      // Nested isolated operations own independent SSA and symbol boundaries.
      // They are not part of the enclosing simulation code unit and may be
      // rewritten by a separately scheduled pass.
      if (operation.hasTrait<OpTrait::IsIsolatedFromAbove>())
        continue;
      addToWorklist(operation.getRegions());
    }

    builder.setInsertionPointToStart(block);
    for (BlockArgument argument : block->getArguments()) {
      const auto *lattice =
          solver.lookupState<Lattice<ConstantValue>>(argument);
      if (!lattice || lattice->getValue().isUninitialized() ||
          !lattice->getValue().getConstantValue())
        continue;
      const ConstantValue &value = lattice->getValue();
      Value constant = folder.getOrCreateConstant(
          builder.getInsertionBlock(), value.getConstantDialect(),
          value.getConstantValue(), argument.getType());
      if (constant)
        argument.replaceAllUsesWith(constant);
    }
  }
}

static LogicalResult solveAndRewriteFunction(ArrayRef<FunctionInfo> functions,
                                             unsigned functionIndex) {
  const FunctionInfo &info = functions[functionIndex];
  sim::SimFuncOp function = info.function;
  if (function.isExternal())
    return success();

  DenseMap<Value, BoundaryFact> seeds;
  addBoundarySeeds(functions, functionIndex, seeds);
  DataFlowConfig config;
  config.setInterprocedural(false);
  DataFlowSolver solver(config);
  solver.load<DeadCodeAnalysis>();
  solver.load<BoundarySparseConstantPropagation>(seeds);
  if (failed(solver.initializeAndRun(function)))
    return failure();
  rewriteFunction(solver, function);
  return success();
}

class ObeliskSimSCCPPass
    : public impl::ObeliskSimSCCPPassBase<ObeliskSimSCCPPass> {
public:
  void runOnOperation() override;
};

void ObeliskSimSCCPPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  SmallVector<FunctionInfo, 0> functions;
  llvm::StringMap<unsigned> symbolToFunction;

  for (sim::SimFuncOp function : design.getBody().getOps<sim::SimFuncOp>()) {
    unsigned index = functions.size();
    StringRef symbol = function.getSymName();
    if (!symbolToFunction.try_emplace(symbol, index).second) {
      function.emitError() << "duplicate simulation function symbol " << symbol;
      return signalPassFailure();
    }
    FunctionInfo &info = functions.emplace_back();
    info.function = function;
    info.symbol = symbol.str();
    info.irOrder = index;
    info.arguments.resize(function.getFunctionType().getNumInputs());
    info.results.resize(function.getFunctionType().getNumResults());
  }

  // Index boundary operations in stable IR order and resolve every direct edge
  // before workers begin. No worker consults a mutable symbol table cache.
  for (auto [functionIndex, info] : llvm::enumerate(functions)) {
    if (info.function.isExternal())
      continue;
    info.function.walk<WalkOrder::PreOrder>([&](Operation *operation) {
      if (operation != info.function.getOperation() &&
          operation->hasTrait<OpTrait::IsIsolatedFromAbove>())
        return WalkResult::skip();
      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        auto found = symbolToFunction.find(call.getCallee());
        std::optional<unsigned> callee;
        if (found != symbolToFunction.end())
          callee = found->second;
        info.sites.push_back({operation, BoundarySiteKind::Call, callee});
        if (callee)
          functions[*callee].callers.push_back(functionIndex);
        return WalkResult::advance();
      }
      if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation)) {
        auto found = symbolToFunction.find(spawn.getCallee());
        std::optional<unsigned> callee;
        if (found != symbolToFunction.end())
          callee = found->second;
        info.sites.push_back({operation, BoundarySiteKind::Spawn, callee});
        return WalkResult::advance();
      }
      if (isa<sim::SimReturnOp>(operation))
        info.sites.push_back(
            {operation, BoundarySiteKind::Return, std::nullopt});
      return WalkResult::advance();
    });
  }
  for (FunctionInfo &info : functions) {
    llvm::sort(info.callers);
    info.callers.erase(std::unique(info.callers.begin(), info.callers.end()),
                       info.callers.end());
  }

  SmallVector<char> hasNonCallUse(functions.size(), false);
  for (auto [index, info] : llvm::enumerate(functions)) {
    std::optional<SymbolTable::UseRange> uses =
        SymbolTable::getSymbolUses(info.function, design);
    if (!uses) {
      hasNonCallUse[index] = true;
      continue;
    }
    for (const SymbolTable::SymbolUse &use : *uses) {
      Operation *user = use.getUser();
      SymbolRefAttr reference = use.getSymbolRef();
      if (auto call = dyn_cast<sim::SimCallOp>(user);
          call && call.getCalleeAttr() == reference)
        continue;
      if (auto spawn = dyn_cast<sim::SimSpawnOp>(user);
          spawn && spawn.getCalleeAttr() == reference)
        continue;
      hasNonCallUse[index] = true;
      break;
    }
  }

  // Entry points can be invoked beyond the indexed direct edges. External
  // declarations and non-call symbol uses additionally obscure their results.
  for (auto [index, info] : llvm::enumerate(functions)) {
    if (SymbolTable::getSymbolVisibility(info.function) !=
        SymbolTable::Visibility::Private)
      for (BoundaryFact &argument : info.arguments)
        argument = getUnknownFact();
    if (info.function.isExternal() || hasNonCallUse[index]) {
      for (BoundaryFact &argument : info.arguments)
        argument = getUnknownFact();
      for (BoundaryFact &result : info.results)
        result = getUnknownFact();
    }
  }

  SmallVector<unsigned> deterministicOrder;
  deterministicOrder.reserve(functions.size());
  for (unsigned index = 0; index < functions.size(); ++index)
    deterministicOrder.push_back(index);
  llvm::sort(deterministicOrder, [&](unsigned lhs, unsigned rhs) {
    if (functions[lhs].symbol != functions[rhs].symbol)
      return functions[lhs].symbol < functions[rhs].symbol;
    return functions[lhs].irOrder < functions[rhs].irOrder;
  });

  SmallVector<char> dirty(functions.size(), true);
  while (llvm::is_contained(dirty, true)) {
    SmallVector<unsigned> wave;
    for (unsigned index : deterministicOrder)
      if (dirty[index]) {
        dirty[index] = false;
        wave.push_back(index);
      }

    std::vector<std::unique_ptr<FunctionObservation>> observations(
        functions.size());
    if (failed(failableParallelForEach(
            design.getContext(), wave, [&](unsigned functionIndex) {
              auto observation = std::make_unique<FunctionObservation>();
              if (failed(
                      analyzeFunction(functions, functionIndex, *observation)))
                return failure();
              observations[functionIndex] = std::move(observation);
              return success();
            })))
      return signalPassFailure();

    // Merge only after the wave has joined. Symbol order followed by each
    // function's IR order makes the fixed point and diagnostics deterministic.
    for (unsigned functionIndex : deterministicOrder) {
      if (!observations[functionIndex])
        continue;
      FunctionInfo &info = functions[functionIndex];
      for (auto [site, observation] :
           llvm::zip_equal(info.sites, observations[functionIndex]->sites)) {
        if (!observation.executable)
          continue;
        if (site.kind == BoundarySiteKind::Return) {
          bool changed = false;
          for (auto [result, contribution] :
               llvm::zip_equal(info.results, observation.operands))
            changed |= mergeFact(result, contribution);
          if (changed)
            for (unsigned caller : info.callers)
              dirty[caller] = true;
          continue;
        }
        if (!site.callee)
          continue;
        FunctionInfo &callee = functions[*site.callee];
        bool changed = false;
        for (auto [argument, contribution] :
             llvm::zip_equal(callee.arguments, observation.operands))
          changed |= mergeFact(argument, contribution);
        if (changed)
          dirty[*site.callee] = true;
      }
    }
  }

  // A bottom that survived the global fixed point has no executable boundary
  // contribution. Make it explicitly unknown before the final local solve so
  // all rewrites are based on initialized, conservative facts.
  for (FunctionInfo &info : functions) {
    for (BoundaryFact &argument : info.arguments)
      if (argument.isUninitialized())
        argument = getUnknownFact();
    for (BoundaryFact &result : info.results)
      if (result.isUninitialized())
        result = getUnknownFact();
  }

  if (failed(failableParallelForEach(
          design.getContext(), deterministicOrder, [&](unsigned index) {
            return solveAndRewriteFunction(functions, index);
          })))
    signalPassFailure();
}

} // namespace
} // namespace obelisk
