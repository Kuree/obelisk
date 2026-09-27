//===- SpecializeStaticStateNBA.cpp - Plan static state/NBA specialization ===//

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/DenseSet.h"

#include <map>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMSPECIALIZESTATICSTATENBAPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {

struct Dependency {
  bool read = false;
  bool write = false;
};

class ObeliskSimSpecializeStaticStateNBAPass
    : public impl::ObeliskSimSpecializeStaticStateNBAPassBase<
          ObeliskSimSpecializeStaticStateNBAPass> {
public:
  using Base::Base;
  void runOnOperation() override;
};

void ObeliskSimSpecializeStaticStateNBAPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  schedule::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph) {
    design.emitOpError(
        "static specialization requires a verified compute graph");
    return signalPassFailure();
  }
  if (maxPackedWidth == 0 ||
      maxPackedWidth > schedule::metadata::maxDirectStaticStateBits) {
    design.emitOpError("max-packed-width must be in the range 1 through 64");
    return signalPassFailure();
  }
  analysis::SimulationVPIAnalysis vpi =
      analysis::SimulationVPIAnalysis::compute(design);

  Builder builder(design.getContext());
  llvm::SmallDenseSet<uint64_t, 8> overrideRoots;
  bool guardAllRoots = false;
  auto resolveStorageRoot = [&](Value value) -> std::optional<uint64_t> {
    llvm::SmallDenseSet<Value, 8> visited;
    while (value && visited.insert(value).second) {
      if (auto argument = dyn_cast<BlockArgument>(value)) {
        auto function =
            dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
        if (!function || argument.getOwner() != &function.getBody().front())
          return std::nullopt;
        auto descriptor = function.getArgAttrOfType<IntegerAttr>(
            argument.getArgNumber(), sim::metadata::descriptorId);
        if (!descriptor || descriptor.getValue().isNegative() ||
            descriptor.getValue().getBitWidth() > 64)
          return std::nullopt;
        return descriptor.getValue().getZExtValue();
      }
      Operation *definition = value.getDefiningOp();
      if (auto storage = dyn_cast_or_null<sim::SimContextStorageOp>(definition))
        return storage.getId();
      if (auto view = dyn_cast_or_null<sim::SimRefExtractOp>(definition))
        value = view.getInput();
      else if (auto view =
                   dyn_cast_or_null<sim::SimRefDynExtractOp>(definition))
        value = view.getInput();
      else if (auto view =
                   dyn_cast_or_null<sim::SimRefSubelementOp>(definition))
        value = view.getInput();
      else if (auto view =
                   dyn_cast_or_null<sim::SimRefArrayElementOp>(definition))
        value = view.getInput();
      else
        return std::nullopt;
    }
    return std::nullopt;
  };
  design.walk([&](Operation *operation) {
    Value target;
    if (auto override = dyn_cast<sim::SimOverrideOp>(operation))
      target = override.getTarget();
    else if (auto release = dyn_cast<sim::SimReleaseOverrideOp>(operation))
      target = release.getTarget();
    else
      return;
    if (!isa<sim::RefType>(target.getType()))
      return;
    if (std::optional<uint64_t> descriptor = resolveStorageRoot(target))
      overrideRoots.insert(*descriptor);
    else
      guardAllRoots = true;
  });

  // IEEE 1800-2023 4.6(b), 10.4.2 (Example 6): delayed and immediate
  // NBAs due in the same slot must retain their execution order, including
  // assignments from different processes. Static accumulators commit before
  // the runtime queue and have no sequence relative to an older delayed NBA.
  // Keep every writer of such a root in the ordered runtime queue. Excluding
  // the root here also covers bytecode's static-site specialization.
  llvm::SmallDenseSet<uint64_t, 8> delayedNBARoots;
  bool unknownDelayedNBARoot = false;
  design.walk([&](sim::SimNBAEnqueueOp enqueue) {
    if (!enqueue.getDelay() ||
        !isa<sim::RefType>(enqueue.getDestination().getType()))
      return;
    if (auto descriptor = resolveStorageRoot(enqueue.getDestination()))
      delayedNBARoots.insert(*descriptor);
    else
      unknownDelayedNBARoot = true;
  });

  llvm::SmallDenseSet<uint64_t, 16> stateEligible;
  DenseMap<uint64_t, unsigned> storageRootWidths;
  SmallVector<sim::SimStorageDeclOp> storages;
  for (sim::SimStorageDeclOp storage :
       design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
    storages.push_back(storage);
    std::optional<unsigned> storageWidth =
        analysis::getSimulationStorageBitWidth(storage.getType());
    if (storageWidth)
      storageRootWidths.try_emplace(storage.getId(), *storageWidth);
    // The policy limits one generated packed access, not the containing
    // storage root.  Fixed slices of a wide register file still have constant
    // offsets and are independently eligible for direct plane access.
    SmallVector<uint64_t, 2> managedRoots;
    bool supported =
        storageWidth && *storageWidth != 0 &&
        sim::getManagedHandleOffsets(storage.getType(), managedRoots) &&
        managedRoots.empty() && !isa<FloatType>(storage.getType());
    if (supported)
      stateEligible.insert(storage.getId());
    else if (missedRemarks)
      storage.emitRemark("direct static state specialization requires a fixed "
                         "packed integer or logic root");
  }

  llvm::SmallDenseSet<uint64_t, 16> nbaEligible;
  SmallVector<int64_t> nbaRoots;
  for (Attribute node : graph.getNodes()) {
    auto commit = dyn_cast<schedule::ComputeNBACommitAttr>(node);
    if (!commit)
      continue;
    schedule::ComputeEffectAttr effect = commit.getEffect();
    if (effect.getResource() != schedule::ComputeResourceKind::Storage ||
        effect.getTarget() != schedule::ComputeTargetKind::Descriptor ||
        effect.getDynamic() || effect.getDeferred() || unknownDelayedNBARoot ||
        delayedNBARoots.contains(effect.getDescriptor()) ||
        !storageRootWidths.contains(effect.getDescriptor()) ||
        !commit.getFrontierSites().empty()) {
      if (missedRemarks)
        design.emitRemark("NBA commit root remains on the generic frontier");
      continue;
    }
    if (!nbaEligible.insert(effect.getDescriptor()).second) {
      design.emitOpError(
          "verified compute graph contains duplicate static NBA roots");
      return signalPassFailure();
    }
    if (effect.getDescriptor() > static_cast<uint64_t>(INT64_MAX)) {
      design.emitOpError(
          "static NBA root identity exceeds the metadata representation");
      return signalPassFailure();
    }
    nbaRoots.push_back(static_cast<int64_t>(effect.getDescriptor()));
  }

  SmallVector<Attribute> roots;
  for (sim::SimStorageDeclOp storage : storages) {
    bool stateSpecialized = stateEligible.contains(storage.getId());
    auto width = storageRootWidths.find(storage.getId());
    bool guarded =
        stateSpecialized && (vpi.allowsWrite() || guardAllRoots ||
                             overrideRoots.contains(storage.getId()));
    roots.push_back(schedule::StaticStateRootAttr::get(
        design.getContext(), storage.getId(),
        width == storageRootWidths.end() ? 0 : width->second,
        stateSpecialized && !guarded, guarded,
        nbaEligible.contains(storage.getId())));
  }

  using DependencyKey = std::pair<std::string, uint64_t>;
  std::map<DependencyKey, Dependency> dependencies;
  for (Attribute node : graph.getNodes()) {
    auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(node);
    if (!fragment)
      continue;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<schedule::ComputeEffectAttr>(effectAttribute);
      if (effect.getResource() != schedule::ComputeResourceKind::Storage ||
          effect.getTarget() != schedule::ComputeTargetKind::Descriptor ||
          effect.getDynamic() || effect.getDeferred() ||
          !stateEligible.contains(effect.getDescriptor()))
        continue;
      Dependency &dependency = dependencies[{
          fragment.getFunction().getValue().str(), effect.getDescriptor()}];
      switch (effect.getEffect()) {
      case schedule::ComputeEffectKind::Read:
      case schedule::ComputeEffectKind::Watch:
        dependency.read = true;
        break;
      case schedule::ComputeEffectKind::Write:
      case schedule::ComputeEffectKind::NBA:
        dependency.write = true;
        break;
      default:
        break;
      }
    }
  }

  SmallVector<Attribute> actorRoots;
  for (auto &[key, dependency] : dependencies)
    actorRoots.push_back(schedule::StaticActorRootAttr::get(
        design.getContext(),
        FlatSymbolRefAttr::get(design.getContext(), key.first), key.second,
        dependency.read, dependency.write));

  auto plan = schedule::StaticSpecializationAttr::get(
      design.getContext(), schedule::metadata::schemaVersion, maxPackedWidth,
      graph, builder.getArrayAttr(roots), builder.getArrayAttr(actorRoots),
      builder.getDenseI64ArrayAttr(nbaRoots));
  ::obelisk::schedule::set<schedule::metadata::staticSpecialization>(design,
                                                                     plan);
}

} // namespace
} // namespace obelisk
