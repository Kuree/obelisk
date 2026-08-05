//===- SCCP.cpp - Threaded interprocedural simulation SCCP ---------------===//

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
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
#include "llvm/ADT/DenseSet.h"
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

struct DriveObservation {
  uint64_t net = 0;
  BoundaryFact value;
};

struct FunctionObservation {
  SmallVector<SiteObservation> sites;
  SmallVector<DriveObservation> drives;
};

static void addNetSeeds(sim::SimFuncOp function,
                        const DenseMap<uint64_t, BoundaryFact> &netFacts,
                        DenseMap<Value, BoundaryFact> &seeds) {
  if (netFacts.empty())
    return;
  analysis::DescriptorProvenanceMap provenance =
      analysis::deriveDescriptorProvenance(function);
  function.walk([&](sim::SimNetReadOp read) {
    auto found = provenance.find(read.getNet());
    if (found == provenance.end() || !found->second.descriptor ||
        found->second.dynamic || found->second.low != 0 ||
        found->second.width != found->second.rootWidth)
      return;
    auto fact = netFacts.find(*found->second.descriptor);
    if (fact != netFacts.end())
      seeds.try_emplace(read.getResult(), fact->second);
  });
}

ValueRange getBoundaryOperands(Operation *operation) {
  if (auto task = dyn_cast<sim::SimTaskCallOp>(operation))
    return task.getArguments();
  return operation->getOperands();
}

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
                                     const DenseMap<uint64_t, BoundaryFact> &netFacts,
                                     FunctionObservation &observation) {
  const FunctionInfo &info = functions[functionIndex];
  sim::SimFuncOp function = info.function;
  if (function.isExternal())
    return success();

  DenseMap<Value, BoundaryFact> seeds;
  addBoundarySeeds(functions, functionIndex, seeds);
  addNetSeeds(function, netFacts, seeds);

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
    if (isa<sim::SimCallOp, sim::SimTaskCallOp>(site.operation)) {
      for (Value operand : getBoundaryOperands(site.operation))
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

  analysis::DescriptorProvenanceMap provenance =
      analysis::deriveDescriptorProvenance(function);
  function.walk([&](Operation *operation) {
    if (!isa<sim::SimDriverDriveOp, sim::SimDriverDriveChangedOp>(operation) ||
        !isExecutable(solver, operation))
      return;
    Value driver;
    Value value;
    if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation)) {
      driver = drive.getDriver();
      value = drive.getValue();
    } else {
      auto changedDrive = cast<sim::SimDriverDriveChangedOp>(operation);
      driver = changedDrive.getDriver();
      value = changedDrive.getValue();
    }
    auto found = provenance.find(driver);
    if (found == provenance.end() || !found->second.descriptor ||
        found->second.dynamic || found->second.low != 0 ||
        found->second.width != found->second.rootWidth)
      return;
    observation.drives.push_back(
        {*found->second.descriptor, getFact(solver, value)});
  });
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
                                             unsigned functionIndex,
                                             const DenseMap<uint64_t, BoundaryFact> &netFacts) {
  const FunctionInfo &info = functions[functionIndex];
  sim::SimFuncOp function = info.function;
  if (function.isExternal())
    return success();

  DenseMap<Value, BoundaryFact> seeds;
  addBoundarySeeds(functions, functionIndex, seeds);
  addNetSeeds(function, netFacts, seeds);
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
  using Base::Base;
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
  for (auto indexedInfo : llvm::enumerate(functions)) {
    unsigned functionIndex = indexedInfo.index();
    FunctionInfo &info = indexedInfo.value();
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
      if (auto task = dyn_cast<sim::SimTaskCallOp>(operation)) {
        auto found = symbolToFunction.find(task.getCallee());
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
      if (auto task = dyn_cast<sim::SimTaskCallOp>(user);
          task && task.getCalleeAttr() == reference)
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
              static const DenseMap<uint64_t, BoundaryFact> noNetFacts;
              if (failed(analyzeFunction(functions, functionIndex, noNetFacts,
                                         *observation)))
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

  // Resolved nets are ordinary scheduler state, so most reads are not SCCP
  // boundaries.  A narrow exception is an invisible, full-width connected
  // component with exactly one full-width driver and one exact value at every
  // executable drive site.  Such a component is immutable after its
  // continuous assignment initializes, and seeding its reads lets local SCCP
  // erase configuration-disabled RTL before compute-graph fusion.  Writable
  // VPI and language overrides deliberately disable this specialization.
  DenseMap<uint64_t, BoundaryFact> netFacts;
  if (vpi == "off") {
    struct NetInfo {
      Type type;
      uint64_t width = 0;
      bool visible = false;
    };
    DenseMap<uint64_t, NetInfo> nets;
    DenseMap<uint64_t, SmallVector<uint64_t>> connections;
    DenseMap<uint64_t, unsigned> driverCounts;
    DenseMap<uint64_t, bool> hasFullDriver;
    DenseSet<uint64_t> invalid;
    bool hasOverride = false;

    for (Operation &operation : design.getBody().front()) {
      if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
        std::optional<uint64_t> width = sim::getProvenanceSpan(net.getType());
        if (!width)
          continue;
        bool visible = net.getObservability() &&
                       *net.getObservability() !=
                           sim::ComputeObservabilityKind::Invisible;
        nets[net.getId()] = {net.getType(), *width, visible};
        connections[net.getId()];
        continue;
      }
      if (auto driver = dyn_cast<sim::SimDriverDeclOp>(operation)) {
        ++driverCounts[driver.getNetId()];
        auto net = nets.find(driver.getNetId());
        std::optional<uint64_t> width =
            sim::getProvenanceSpan(driver.getType());
        uint64_t low = driver.getDrivenLowAttr()
                           ? driver.getDrivenLowAttr().getValue().getZExtValue()
                           : 0;
        uint64_t drivenWidth =
            driver.getDrivenWidthAttr()
                ? driver.getDrivenWidthAttr().getValue().getZExtValue()
                : width.value_or(0);
        hasFullDriver[driver.getNetId()] =
            net != nets.end() && width && driver.getType() == net->second.type &&
            low == 0 && drivenWidth == net->second.width;
      }
    }
    for (sim::SimNetConnectDeclOp connection :
         design.getBody().front().getOps<sim::SimNetConnectDeclOp>()) {
      uint64_t lhs = connection.getLhsNetId();
      uint64_t rhs = connection.getRhsNetId();
      connections[lhs].push_back(rhs);
      connections[rhs].push_back(lhs);
      auto lhsInfo = nets.find(lhs);
      auto rhsInfo = nets.find(rhs);
      if (lhsInfo == nets.end() || rhsInfo == nets.end() ||
          lhsInfo->second.type != rhsInfo->second.type ||
          connection.getLhsOffset() != 0 || connection.getRhsOffset() != 0 ||
          connection.getWidth() != lhsInfo->second.width ||
          connection.getWidth() != rhsInfo->second.width ||
          connection.getRhsReversed()) {
        invalid.insert(lhs);
        invalid.insert(rhs);
      }
    }
    design.walk([&](Operation *operation) {
      hasOverride |= isa<sim::SimOverrideOp, sim::SimReleaseOverrideOp>(operation);
    });

    DenseMap<uint64_t, SmallVector<uint64_t>> components;
    DenseMap<uint64_t, uint64_t> representatives;
    DenseSet<uint64_t> visited;
    if (!hasOverride) {
      for (auto [root, unused] : connections) {
        if (!visited.insert(root).second)
          continue;
        SmallVector<uint64_t> members{root};
        uint64_t representative = root;
        bool eligible = true;
        unsigned drivers = 0;
        bool fullDriver = false;
        for (size_t index = 0; index != members.size(); ++index) {
          uint64_t member = members[index];
          representative = std::min(representative, member);
          auto info = nets.find(member);
          eligible &= info != nets.end() && !info->second.visible &&
                      !invalid.contains(member);
          drivers += driverCounts.lookup(member);
          fullDriver |= hasFullDriver.lookup(member);
          for (uint64_t neighbor : connections.lookup(member))
            if (visited.insert(neighbor).second)
              members.push_back(neighbor);
        }
        eligible &= drivers == 1 && fullDriver;
        if (!eligible)
          continue;
        components[representative] = members;
        for (uint64_t member : members)
          representatives[member] = representative;
      }
    }

    // Exact constants can expose another exact constant one net downstream.
    // Grow the facts monotonically until a wave discovers nothing new.
    while (!components.empty()) {
      std::vector<std::unique_ptr<FunctionObservation>> observations(
          functions.size());
      if (failed(failableParallelForEach(
              design.getContext(), deterministicOrder, [&](unsigned index) {
                auto observation = std::make_unique<FunctionObservation>();
                if (failed(analyzeFunction(functions, index, netFacts,
                                           *observation)))
                  return failure();
                observations[index] = std::move(observation);
                return success();
              }))) {
        signalPassFailure();
        return;
      }

      DenseMap<uint64_t, BoundaryFact> componentFacts;
      DenseSet<uint64_t> observedComponents;
      for (const auto &observation : observations) {
        if (!observation)
          continue;
        for (const DriveObservation &drive : observation->drives) {
          auto representative = representatives.find(drive.net);
          if (representative == representatives.end())
            continue;
          observedComponents.insert(representative->second);
          auto [fact, inserted] = componentFacts.try_emplace(
              representative->second, drive.value);
          if (!inserted)
            mergeFact(fact->second, drive.value);
        }
      }

      bool changed = false;
      for (auto [representative, members] : components) {
        auto fact = componentFacts.find(representative);
        if (fact == componentFacts.end() ||
            !observedComponents.contains(representative) ||
            fact->second.isUninitialized() ||
            !fact->second.getConstantValue())
          continue;
        for (uint64_t member : members)
          changed |= netFacts.try_emplace(member, fact->second).second;
      }
      if (!changed)
        break;
    }
  }

  if (failed(failableParallelForEach(
          design.getContext(), deterministicOrder, [&](unsigned index) {
            return solveAndRewriteFunction(functions, index, netFacts);
          })))
    signalPassFailure();
}

} // namespace
} // namespace obelisk
