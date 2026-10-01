//===- PropagateInitializedStorage.cpp
//-------------------------------------===//

#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/StorageWriteAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/PassManager.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPROPAGATEINITIALIZEDSTORAGEPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {
namespace sim = ::obelisk::sim;

struct StorageAccess {
  Operation *operation;
  sim::SimFuncOp function;
  uint64_t low = 0;
  uint64_t width = 0;
};

struct StorageCandidate {
  Type type;
  SmallVector<StorageAccess> writes;
  SmallVector<StorageAccess> reads;
  bool invalid = false;
};

struct InitializedStorageReplacements {
  explicit InitializedStorageReplacements(Operation *) {}
  struct Replacement {
    sim::SimRefLoadOp load;
    IntegerAttr value;
    IntegerAttr unknown;
  };
  DenseMap<Operation *, SmallVector<Replacement>> functions;
};

class PropagateInitializedStorageFunctionPass final
    : public PassWrapper<PropagateInitializedStorageFunctionPass,
                         OperationPass<sim::SimFuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      PropagateInitializedStorageFunctionPass)
  StringRef getArgument() const final {
    return "obelisk-sim-propagate-initialized-storage-function";
  }
  void runOnOperation() override {
    auto function = getOperation();
    auto cached = getCachedParentAnalysis<InitializedStorageReplacements>(
        function->getParentOfType<sim::SimDesignOp>());
    if (!cached) {
      function.emitError(
          "initialized storage rewriting requires a frozen proof");
      return signalPassFailure();
    }
    const auto &functions = cached->get().functions;
    auto found = functions.find(function);
    if (found == functions.end())
      return;
    for (const auto &planned : found->second) {
      auto load = planned.load;
      OpBuilder builder(load);
      Type type = load.getResult().getType();
      Value replacement;
      if (auto integer = dyn_cast<IntegerType>(type))
        replacement = arith::ConstantOp::create(
            builder, load.getLoc(), integer,
            IntegerAttr::get(integer, planned.value.getValue() &
                                          ~planned.unknown.getValue()));
      else
        replacement = sim::SimLogicConstantOp::create(
            builder, load.getLoc(), cast<sim::LogicType>(type), planned.value,
            planned.unknown);
      load.getResult().replaceAllUsesWith(replacement);
      load.erase();
    }
  }
};

class ObeliskSimPropagateInitializedStoragePass
    : public impl::ObeliskSimPropagateInitializedStoragePassBase<
          ObeliskSimPropagateInitializedStoragePass> {
public:
  using Base::Base;

private:
  void runOnOperation() override {
    sim::SimDesignOp design = getOperation();
    if (vpi == "full")
      return;
    if (vpi != "off" && vpi != "read") {
      design.emitOpError("VPI mode must be off, read, or full");
      return signalPassFailure();
    }
    if (design.getBody().empty())
      return;

    DenseMap<uint64_t, StorageCandidate> candidates;
    for (sim::SimStorageDeclOp storage :
         design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
      Type type = storage.getType();
      if (storage.getLifetime() == sim::Lifetime::Design &&
          (!storage.getObservability() ||
           *storage.getObservability() !=
               schedule::ComputeObservabilityKind::ExternallyWritable) &&
          isa<IntegerType, sim::LogicType>(type))
        candidates.try_emplace(storage.getId(), StorageCandidate{type});
    }
    if (candidates.empty())
      return;

    sim::SimFuncOp root;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      if (function.getEntryKind() == sim::EntryKind::RootInitializer) {
        if (root)
          return;
        root = function;
      }
    if (!root || root.getBody().empty())
      return;

    // This pass rewrites loads but never creates, erases, or renames symbols.
    // Cache the design table once instead of rescanning its block per call.
    SymbolTable symbols(design);
    DenseMap<Operation *, unsigned> callCounts;
    DenseSet<Operation *> initializedBeforeSpawn;
    DenseMap<Operation *, SmallVector<sim::SimFuncOp>> callees;
    DenseMap<Operation *, bool> simpleInitializers;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      bool simple =
          function.getBody().hasOneBlock() &&
          isa<sim::SimReturnOp>(function.getBody().front().getTerminator());
      function.walk([&](Operation *op) {
        if (op == function.getOperation())
          return;
        FlatSymbolRefAttr target;
        if (auto call = dyn_cast<sim::SimCallOp>(op))
          target = call.getCalleeAttr();
        else if (auto call = dyn_cast<sim::SimTaskCallOp>(op))
          target = call.getCalleeAttr();
        else if (auto spawn = dyn_cast<sim::SimSpawnOp>(op))
          target = spawn.getCalleeAttr();
        if (target)
          if (auto callee = symbols.lookup<sim::SimFuncOp>(target.getValue())) {
            ++callCounts[callee];
            if (isa<sim::SimCallOp>(op))
              callees[function].push_back(callee);
          }
        simple &= op->getNumRegions() == 0 &&
                  (isMemoryEffectFree(op) ||
                   isa<sim::SimRefStoreOp, sim::SimReturnOp>(op));
      });
      simpleInitializers[function] = simple;
    }
    if (callCounts.lookup(root) != 0)
      return;
    SmallVector<Operation *> definitions, startupBoundaries;
    root.walk([&](Operation *op) {
      if (isa<sim::SimRefStoreOp, sim::SimCallOp>(op))
        definitions.push_back(op);
      if (isa<sim::SimSpawnOp, sim::SimReturnOp>(op))
        startupBoundaries.push_back(op);
    });
    // LRM 6.8: initialization precedes *every* process startup. A safe prefix
    // on just one branch does not prove a conditional initializer executed.
    // Use dense must definitions at all startup/return boundaries and count
    // execution over the entire root lifetime, without resetting at delays.
    analysis::NoBarrierAnalysis prefix(root, [&](Operation *op) {
      if (op == root.getOperation())
        return false;
      if (auto call = dyn_cast<sim::SimCallOp>(op)) {
        auto callee = symbols.lookup<sim::SimFuncOp>(call.getCallee());
        return !callee || !simpleInitializers.lookup(callee);
      }
      return op->getNumRegions() != 0 ||
             !(isMemoryEffectFree(op) ||
               isa<sim::SimRefStoreOp, sim::SimReturnOp, BranchOpInterface>(
                   op));
    });
    analysis::MustDefinitionAnalysis initialized(
        root, definitions, [](Operation *) { return false; });
    analysis::WriteExecutionBounds executions(root, definitions, false, false);
    for (Operation *definition : definitions) {
      if (!prefix.isSafeBefore(definition) ||
          !executions.executesAtMostOnce(definition) ||
          startupBoundaries.empty() ||
          !llvm::all_of(startupBoundaries, [&](Operation *boundary) {
            return initialized.containsBefore(definition, boundary);
          }))
        continue;
      if (auto store = dyn_cast<sim::SimRefStoreOp>(definition))
        initializedBeforeSpawn.insert(store);
      else if (auto call = dyn_cast<sim::SimCallOp>(definition)) {
        auto callee = symbols.lookup<sim::SimFuncOp>(call.getCallee());
        if (callee && simpleInitializers.lookup(callee) &&
            callCounts.lookup(callee) == 1)
          for (auto store :
               callee.getBody().front().getOps<sim::SimRefStoreOp>())
            initializedBeforeSpawn.insert(store);
      }
    }
    SmallVector<sim::SimFuncOp> rootCalls = callees.lookup(root);
    // A read in any root-called initializer can precede another initializer.
    // Keep that entire call closure out of the rewrite, regardless of the
    // textual order of the root's calls.
    DenseSet<Operation *> rootCallClosure;
    while (!rootCalls.empty()) {
      sim::SimFuncOp function = rootCalls.pop_back_val();
      if (!rootCallClosure.insert(function.getOperation()).second)
        continue;
      llvm::append_range(rootCalls, callees.lookup(function));
    }

    analysis::HandleDataflowAnalysis provenanceAnalysis(design);
    bool unknownWrite = false;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      analysis::HandleFacts provenance = provenanceAnalysis.derive(function);
      function.walk([&](Operation *operation) {
        // Some effects can reach storage without carrying a sim.ref operand
        // (for example, a class dispatch or an erased argument reference).
        // Only calls into inspected code and writes through ordinary refs
        // can participate in this whole-storage proof.
        if (auto effects = dyn_cast<MemoryEffectOpInterface>(operation)) {
          SmallVector<MemoryEffects::EffectInstance> instances;
          effects.getEffects(instances);
          for (const auto &effect : instances) {
            if (!isa<MemoryEffects::Write>(effect.getEffect()) ||
                !isa<sim::StorageResource>(effect.getResource()))
              continue;
            if (isa<sim::SimRefStoreOp, sim::SimRefCopyOp, sim::SimOverrideOp,
                    sim::SimDynamicOverrideOp, sim::SimReleaseOverrideOp,
                    sim::SimRefStoreInertialPathOp>(operation))
              continue;
            if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
              sim::SimFuncOp callee =
                  symbols.lookup<sim::SimFuncOp>(call.getCallee());
              unknownWrite |= !callee || callee.isExternal();
              continue;
            }
            if (auto call = dyn_cast<sim::SimTaskCallOp>(operation)) {
              sim::SimFuncOp callee =
                  symbols.lookup<sim::SimFuncOp>(call.getCallee());
              unknownWrite |= !callee || callee.isExternal();
              continue;
            }
            unknownWrite = true;
          }
        }
        for (Value operand : operation->getOperands()) {
          if (!isa<sim::RefType>(operand.getType()))
            continue;
          auto found = provenance.find(operand);
          if (found != provenance.end() &&
              found->second.resource !=
                  schedule::ComputeResourceKind::Storage &&
              found->second.resource != schedule::ComputeResourceKind::Unknown)
            continue;
          if (found == provenance.end() || !found->second.descriptor ||
              found->second.resource !=
                  schedule::ComputeResourceKind::Storage) {
            // An unknown destination might alias any design storage. Local
            // references have a distinct provenance and do not reach here.
            if (!isa<sim::SimRefLoadOp, sim::SimSuspendChangeOp,
                     sim::SimSuspendEdgeOp, sim::SimSuspendAnyOp,
                     sim::SimSuspendObserveOp>(operation))
              unknownWrite = true;
            continue;
          }
          auto candidate = candidates.find(*found->second.descriptor);
          if (candidate == candidates.end())
            continue;
          StorageCandidate &info = candidate->second;
          const analysis::HandleFact &span = found->second;
          // A fixed view is an alias, not a write. Inspect its consumers too;
          // partial stores and opaque escapes still invalidate the root.
          if (isa<sim::SimRefExtractOp>(operation))
            continue;
          if (auto load = dyn_cast<sim::SimRefLoadOp>(operation)) {
            auto width = sim::getProvenanceSpan(load.getResult().getType());
            // Provenance can join different CFG slices into a covering span.
            // Only an exact, in-bounds scalar span denotes one fixed value.
            if (load.getReference() == operand && !span.dynamic && width &&
                *width == span.width && span.width != 0 &&
                span.low <= span.rootWidth &&
                span.width <= span.rootWidth - span.low &&
                isa<IntegerType, sim::LogicType>(load.getResult().getType()))
              info.reads.push_back({operation, function, span.low, span.width});
            else
              info.invalid = true;
          } else if (auto store = dyn_cast<sim::SimRefStoreOp>(operation)) {
            if (store.getReference() == operand && !span.dynamic &&
                span.low == 0 && span.width != 0 &&
                span.width == span.rootWidth)
              info.writes.push_back({operation, function});
            else
              info.invalid = true;
          } else if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
            info.invalid |= !symbols.lookup<sim::SimFuncOp>(call.getCallee());
          } else if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation)) {
            info.invalid |= !symbols.lookup<sim::SimFuncOp>(spawn.getCallee());
          } else {
            // Delayed writes, force/release, and opaque reference users are
            // not admitted by this exact proof.
            info.invalid = true;
          }
        }
      });
    }
    if (unknownWrite)
      return;

    auto &replacements = getAnalysis<InitializedStorageReplacements>();
    for (auto &[descriptor, info] : candidates) {
      (void)descriptor;
      if (info.invalid || info.writes.size() != 1 || info.reads.empty())
        continue;
      StorageAccess write = info.writes.front();
      if (!initializedBeforeSpawn.contains(write.operation))
        continue;
      auto store = cast<sim::SimRefStoreOp>(write.operation);
      Operation *constant = store.getValue().getDefiningOp();
      if (!constant ||
          !isa<arith::ConstantIntOp, sim::SimLogicConstantOp>(constant) ||
          store.getValue().getType() != info.type)
        continue;
      bool rootRead = false;
      for (StorageAccess read : info.reads)
        rootRead |= read.function == root ||
                    rootCallClosure.contains(read.function.getOperation());
      if (rootRead)
        continue;
      for (const StorageAccess &read : info.reads) {
        auto load = cast<sim::SimRefLoadOp>(read.operation);

        APInt value, unknown;
        if (auto integer = dyn_cast<arith::ConstantIntOp>(constant)) {
          value = cast<IntegerAttr>(integer.getValue()).getValue();
          unknown = APInt::getZero(value.getBitWidth());
        } else {
          auto logic = cast<sim::SimLogicConstantOp>(constant);
          value = logic.getValue();
          unknown = logic.getUnknown();
        }
        // IEEE 1800-2023 6.8, 11.5.1: slice both planes without losing Z.
        value = value.extractBits(read.width, read.low);
        unknown = unknown.extractBits(read.width, read.low);
        auto integer = IntegerType::get(design.getContext(), read.width);
        replacements.functions[read.function].push_back(
            {load, IntegerAttr::get(integer, value),
             IntegerAttr::get(integer, unknown)});
      }
    }
    if (!replacements.functions.empty()) {
      OpPassManager pipeline(sim::SimDesignOp::getOperationName());
      pipeline.nest<sim::SimFuncOp>().addPass(
          std::make_unique<PropagateInitializedStorageFunctionPass>());
      if (failed(runPipeline(pipeline, design)))
        signalPassFailure();
    }
  }
};

} // namespace
} // namespace obelisk
