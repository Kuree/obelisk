//===- PropagateInitializedStorage.cpp -------------------------------------===//

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

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
};

struct StorageCandidate {
  Type type;
  SmallVector<StorageAccess> writes;
  SmallVector<StorageAccess> reads;
  bool invalid = false;
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
               sim::ComputeObservabilityKind::ExternallyWritable) &&
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
    if (!root || !root.getBody().hasOneBlock())
      return;

    DenseMap<Operation *, unsigned> callCounts;
    DenseSet<Operation *> calledBeforeSpawn;
    SmallVector<sim::SimFuncOp> rootCalls;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      function.walk([&](sim::SimCallOp call) {
        if (sim::SimFuncOp callee =
                design.lookupSymbol<sim::SimFuncOp>(call.getCallee()))
          ++callCounts[callee.getOperation()];
      });
      function.walk([&](sim::SimSpawnOp spawn) {
        if (sim::SimFuncOp callee =
                design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee()))
          ++callCounts[callee.getOperation()];
      });
      function.walk([&](sim::SimTaskCallOp call) {
        if (sim::SimFuncOp callee =
                design.lookupSymbol<sim::SimFuncOp>(call.getCallee()))
          ++callCounts[callee.getOperation()];
      });
    }
    if (callCounts.lookup(root.getOperation()) != 0)
      return;
    bool safePrefix = true;
    for (Operation &operation : root.getBody().front()) {
      if (isa<sim::SimSpawnOp>(operation))
        safePrefix = false;
      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        sim::SimFuncOp callee =
            design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
        if (!callee)
          return;
        rootCalls.push_back(callee);
        // Before the candidate's initializer, every root call must itself
        // finish without suspending or creating a process. Otherwise a read
        // could run before the initializer despite the root's block order.
        bool simpleInitializer = callee.getBody().hasOneBlock();
        if (simpleInitializer)
          for (Operation &nested : callee.getBody().front())
            simpleInitializer &=
                isa<arith::ConstantOp, sim::SimLogicConstantOp,
                    sim::SimContextStorageOp, sim::SimRefStoreOp,
                    sim::SimReturnOp>(nested);
        safePrefix &= simpleInitializer;
        if (safePrefix)
          calledBeforeSpawn.insert(callee.getOperation());
      } else if (!isa<arith::ConstantOp, sim::SimLogicConstantOp,
                      sim::SimContextStorageOp>(operation))
        safePrefix = false;
    }
    // A read in any root-called initializer can precede another initializer.
    // Keep that entire call closure out of the rewrite, regardless of the
    // textual order of the root's calls.
    DenseSet<Operation *> rootCallClosure;
    while (!rootCalls.empty()) {
      sim::SimFuncOp function = rootCalls.pop_back_val();
      if (!rootCallClosure.insert(function.getOperation()).second)
        continue;
      function.walk([&](sim::SimCallOp call) {
        if (sim::SimFuncOp callee =
                design.lookupSymbol<sim::SimFuncOp>(call.getCallee()))
          rootCalls.push_back(callee);
      });
    }

    analysis::DescriptorProvenanceAnalysis provenanceAnalysis(design);
    bool unknownWrite = false;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      analysis::DescriptorProvenanceMap provenance =
          provenanceAnalysis.derive(function);
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
                  design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
              unknownWrite |= !callee || callee.isExternal();
              continue;
            }
            if (auto call = dyn_cast<sim::SimTaskCallOp>(operation)) {
              sim::SimFuncOp callee =
                  design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
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
              found->second.resource != sim::ComputeResourceKind::Storage &&
              found->second.resource != sim::ComputeResourceKind::Unknown)
            continue;
          if (found == provenance.end() || !found->second.descriptor ||
              found->second.resource != sim::ComputeResourceKind::Storage) {
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
          const analysis::DescriptorProvenance &span = found->second;
          bool whole = !span.dynamic && span.low == 0 && span.width != 0 &&
                       span.width == span.rootWidth;
          if (!whole) {
            info.invalid = true;
            continue;
          }
          if (auto load = dyn_cast<sim::SimRefLoadOp>(operation)) {
            if (load.getReference() == operand)
              info.reads.push_back({operation, function});
            else
              info.invalid = true;
          } else if (auto store = dyn_cast<sim::SimRefStoreOp>(operation)) {
            if (store.getReference() == operand)
              info.writes.push_back({operation, function});
            else
              info.invalid = true;
          } else if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
            info.invalid |=
                !design.lookupSymbol<sim::SimFuncOp>(call.getCallee());
          } else if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation)) {
            info.invalid |=
                !design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee());
          } else {
            // No aliases, partial references, delayed writes, force/release,
            // or opaque reference users are admitted by this exact proof.
            info.invalid = true;
          }
        }
      });
    }
    if (unknownWrite)
      return;

    for (auto &[descriptor, info] : candidates) {
      (void)descriptor;
      if (info.invalid || info.writes.size() != 1 || info.reads.empty())
        continue;
      StorageAccess write = info.writes.front();
      sim::SimFuncOp initializer = write.function;
      if (!calledBeforeSpawn.contains(initializer.getOperation()) ||
          callCounts.lookup(initializer.getOperation()) != 1 ||
          !initializer.getBody().hasOneBlock() ||
          write.operation->getBlock() != &initializer.getBody().front())
        continue;
      auto store = cast<sim::SimRefStoreOp>(write.operation);
      Operation *constant = store.getValue().getDefiningOp();
      if (!constant ||
          !isa<arith::ConstantIntOp, sim::SimLogicConstantOp>(constant) ||
          store.getValue().getType() != info.type)
        continue;
      bool initializerIsStraightLine = true;
      for (Operation &operation : initializer.getBody().front())
        initializerIsStraightLine &=
            isa<arith::ConstantOp, sim::SimLogicConstantOp,
                sim::SimContextStorageOp, sim::SimRefStoreOp, sim::SimReturnOp>(
                operation);
      if (!initializerIsStraightLine)
        continue;
      bool rootRead = false;
      for (StorageAccess read : info.reads)
        rootRead |= read.function == root ||
                    rootCallClosure.contains(read.function.getOperation());
      if (rootRead)
        continue;
      for (const StorageAccess &read : info.reads) {
        auto load = cast<sim::SimRefLoadOp>(read.operation);
        if (load.getResult().getType() != info.type)
          continue;
        OpBuilder builder(load);
        Operation *copy = builder.clone(*constant);
        load.getResult().replaceAllUsesWith(copy->getResult(0));
        load.erase();
      }
    }
  }
};

} // namespace
} // namespace obelisk
