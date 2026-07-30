//===- SimulationNBAPlanning.cpp - Native static NBA plan support -------===//

#include "SimulationNBALowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Twine.h"

#include <limits>

using namespace mlir;

namespace obelisk::detail {
namespace {

/// A fixed reference proven to lower to a constant stable handle.
struct StaticNBADestination {
  uint32_t staticID;
  uint64_t offset;
};

std::optional<StaticNBADestination>
resolveStaticNBADestination(Value value, const NativeStateLayout &layout,
                            DenseSet<Value> &active) {
  if (!value || !active.insert(value).second)
    return std::nullopt;
  auto finish = [&](std::optional<StaticNBADestination> result) {
    active.erase(value);
    return result;
  };
  auto addOffset = [&](std::optional<StaticNBADestination> base,
                       uint64_t offset) -> std::optional<StaticNBADestination> {
    if (!base || offset > std::numeric_limits<uint64_t>::max() - base->offset)
      return std::nullopt;
    base->offset += offset;
    return base;
  };
  auto resolveDescriptor =
      [&](uint64_t descriptor) -> std::optional<StaticNBADestination> {
    auto handle = layout.storage.find(descriptor);
    if (handle == layout.storage.end())
      return std::nullopt;
    obelisk_rt_stable_handle_v1 decoded{};
    if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
        decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset < 0)
      return std::nullopt;
    return StaticNBADestination{decoded.id,
                                static_cast<uint64_t>(decoded.offset)};
  };

  if (auto argument = dyn_cast<BlockArgument>(value)) {
    auto function =
        dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
    if (!function)
      return finish(std::nullopt);
    // Capture specialization has already replaced every direct context
    // descriptor with a SimContextStorageOp. A surviving entry argument may
    // be a view or another runtime-selected handle; descriptor provenance does
    // not prove that native lowering will materialize it as a constant.
    if (argument.getOwner() == &function.getBody().front())
      return finish(std::nullopt);

    std::optional<StaticNBADestination> resolved;
    Block *block = argument.getOwner();
    for (Block *predecessor : block->getPredecessors()) {
      Operation *terminator = predecessor->getTerminator();
      auto branch = dyn_cast<BranchOpInterface>(terminator);
      if (!branch)
        return finish(std::nullopt);
      for (unsigned successor = 0; successor != terminator->getNumSuccessors();
           ++successor) {
        if (terminator->getSuccessor(successor) != block)
          continue;
        SuccessorOperands operands = branch.getSuccessorOperands(successor);
        unsigned index = argument.getArgNumber();
        if (index >= operands.size() || operands.isOperandProduced(index))
          return finish(std::nullopt);
        std::optional<StaticNBADestination> incoming =
            resolveStaticNBADestination(operands[index], layout, active);
        if (!incoming ||
            (resolved && (resolved->staticID != incoming->staticID ||
                          resolved->offset != incoming->offset)))
          return finish(std::nullopt);
        resolved = incoming;
      }
    }
    return finish(resolved);
  }

  if (auto storage = value.getDefiningOp<sim::SimContextStorageOp>())
    return finish(resolveDescriptor(storage.getId()));
  if (auto view = value.getDefiningOp<sim::SimRefExtractOp>())
    return finish(
        addOffset(resolveStaticNBADestination(view.getInput(), layout, active),
                  view.getLowBit()));
  if (auto view = value.getDefiningOp<sim::SimRefSubelementOp>()) {
    uint64_t offset = 0;
    Type type = cast<sim::RefType>(view.getInput().getType()).getElementType();
    for (int64_t index : view.getIndices()) {
      if (index < 0)
        return finish(std::nullopt);
      auto child = sim::getAggregateProvenanceSubelement(
          type, static_cast<unsigned>(index));
      if (!child ||
          child->first > std::numeric_limits<uint64_t>::max() - offset)
        return finish(std::nullopt);
      offset += child->first;
      type = sim::getAggregateElementType(type, static_cast<unsigned>(index));
    }
    return finish(addOffset(
        resolveStaticNBADestination(view.getInput(), layout, active), offset));
  }
  return finish(std::nullopt);
}

std::optional<StaticNBADestination>
resolveStaticNBADestination(Value value, const NativeStateLayout &layout) {
  DenseSet<Value> active;
  return resolveStaticNBADestination(value, layout, active);
}

bool isNonInvalidatingStaticNBA(
    sim::SimNBAEnqueueOp op,
    const DenseMap<uint64_t, uint32_t> &staticNBASiteRoots,
    ArrayRef<obelisk_rt_static_nba_root> staticNBARoots,
    const NativeStateLayout &stateLayout) {
  sim::NBASiteAttr site = op.getSiteAttr();
  std::optional<unsigned> width = nativeStateWidth(op.getValue().getType());
  if (!site || !width || *width > 64 || op.getDelay() || site.getTiming() ||
      site.getStorage() == sim::ComputeNBAStorageKind::DynamicFrontier)
    return false;
  auto planned = staticNBASiteRoots.find(site.getId());
  if (planned == staticNBASiteRoots.end() ||
      planned->second >= staticNBARoots.size())
    return false;
  std::optional<StaticNBADestination> destination =
      resolveStaticNBADestination(op.getDestination(), stateLayout);
  const obelisk_rt_static_nba_root &root = staticNBARoots[planned->second];
  return destination && destination->staticID == root.static_state &&
         destination->offset <= root.bit_width &&
         *width <= root.bit_width - destination->offset;
}

} // namespace

LogicalResult markCleanStaticNBAsInGuardedBodies(
    ModuleOp module, bool enabled,
    const DenseMap<uint64_t, uint32_t> &staticNBASiteRoots,
    ArrayRef<obelisk_rt_static_nba_root> staticNBARoots,
    const NativeStateLayout &stateLayout) {
  SmallVector<sim::SimFuncOp> functions;
  module.walk([&](sim::SimFuncOp function) {
    if (function->hasAttr(sim::metadata::nativeGuardedSpecializationBody))
      functions.push_back(function);
  });

  for (sim::SimFuncOp function : functions) {
    function->removeAttr(sim::metadata::nativeGuardedSpecializationBody);
    if (!enabled)
      continue;

    Operation *suspension = nullptr;
    bool multipleSuspensions = false;
    function.walk([&](Operation *operation) {
      if (!sim::isSuspensionOp(operation))
        return;
      multipleSuspensions |= suspension != nullptr;
      suspension = operation;
    });
    if (multipleSuspensions || !suspension ||
        suspension->getNumSuccessors() != 1)
      return function.emitOpError(
                 "has invalid guarded-specialization activation structure"),
             failure();

    Block *activationEntry = suspension->getSuccessor(0);
    if (activationEntry == &function.getBody().front() ||
        activationEntry->getParent() != &function.getBody())
      return function.emitOpError(
                 "has invalid guarded-specialization continuation"),
             failure();

    // Runtime dispatch selects native or bytecode execution at this activation
    // boundary. The native body is the clean form; dirty actors never enter it.
    SmallVector<Block *> activationBlocks;
    SmallVector<Block *> pending{activationEntry};
    llvm::SmallPtrSet<Block *, 16> visited;
    Block *suspensionBlock = suspension->getBlock();
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (block == suspensionBlock || !visited.insert(block).second)
        continue;
      if (block->getParent() != &function.getBody())
        return function.emitOpError(
                   "guarded-specialization body leaves its process region"),
               failure();
      if (llvm::any_of(*block, [](Operation &operation) {
            return sim::isSuspensionOp(&operation);
          }))
        return function.emitOpError(
                   "guarded-specialization body contains a suspension"),
               failure();
      activationBlocks.push_back(block);
      for (Block *successor : block->getSuccessors())
        if (successor != suspensionBlock)
          pending.push_back(successor);
    }
    if (activationBlocks.empty())
      return function.emitOpError("has an empty guarded-specialization body"),
             failure();

    // A generic enqueue claims its root's slow path for the rest of the slot.
    // Elide per-site guards only when every reachable enqueue is statically
    // staged and cannot invalidate that invariant mid-activation.
    bool nbaActivationIsNonInvalidating = true;
    for (Block *block : activationBlocks)
      block->walk([&](sim::SimNBAEnqueueOp nba) {
        nbaActivationIsNonInvalidating &= isNonInvalidatingStaticNBA(
            nba, staticNBASiteRoots, staticNBARoots, stateLayout);
      });

    for (Block *source : activationBlocks)
      for (Operation &operation : *source)
        operation.walk([&](Operation *nested) {
          if (nbaActivationIsNonInvalidating &&
              isa<sim::SimNBAEnqueueOp>(nested))
            nested->setAttr(assumeCleanSpecializationAttr,
                            UnitAttr::get(function.getContext()));
        });
  }
  return success();
}

LogicalResult
materializeGeneratedNBAAccumulators(ModuleOp module,
                                    const NativeStaticNBAPlan &plan) {
  if (plan.generatedAccumulators.size() != plan.roots.size())
    return module.emitError("generated NBA accumulator plan is malformed");
  OpBuilder builder(module.getContext());
  Location location = module.getLoc();
  Type storageType = LLVM::LLVMArrayType::get(
      builder.getI8Type(), sizeof(obelisk_rt_generated_nba_accumulator_256));
  for (StringRef name : plan.generatedAccumulators) {
    if (name.empty())
      continue;
    if (module.lookupSymbol<LLVM::GlobalOp>(name))
      return module.emitError("generated NBA accumulator symbol is duplicated");
    builder.setInsertionPointToStart(module.getBody());
    auto global =
        LLVM::GlobalOp::create(builder, location, storageType, false,
                               LLVM::Linkage::Internal, name, Attribute{}, 32);
    Block *initializer = new Block;
    global.getInitializerRegion().push_back(initializer);
    builder.setInsertionPointToStart(initializer);
    LLVM::ReturnOp::create(
        builder, location,
        LLVM::ZeroOp::create(builder, location, storageType));
  }
  return success();
}

FailureOr<NativeStaticNBAPlan>
buildNativeStaticNBAPlan(ModuleOp module, const NativeStateLayout &stateLayout,
                         ArrayRef<sim::ComputeNBACommitAttr> orderedCommits,
                         bool enabled) {
  NativeStaticNBAPlan plan;
  if (!enabled)
    return plan;
  for (sim::ComputeNBACommitAttr commit : orderedCommits) {
    sim::ComputeEffectAttr effect = commit.getEffect();
    if (effect.getResource() != sim::ComputeResourceKind::Storage ||
        effect.getTarget() != sim::ComputeTargetKind::Descriptor ||
        effect.getDynamic() || effect.getDeferred())
      return module.emitError(
                 "static NBA commit does not identify one fixed storage root"),
             failure();
    auto handle = stateLayout.storage.find(effect.getDescriptor());
    if (handle == stateLayout.storage.end())
      return module.emitError(
                 "static NBA commit references an unknown storage descriptor"),
             failure();
    obelisk_rt_stable_handle_v1 decoded{};
    if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
        decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset != 0)
      return module.emitError(
                 "static NBA commit has an invalid native state root"),
             failure();
    auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
      return candidate.handleID == decoded.id;
    });
    if (bound == stateLayout.bounds.end())
      return module.emitError(
                 "static NBA commit root is absent from native state layout"),
             failure();
    if (!stateLayout.nbaHandles.contains(decoded.id) ||
        !commit.getFrontierSites().empty())
      continue;
    uint32_t root = static_cast<uint32_t>(plan.roots.size());
    plan.roots.push_back({commit.getId(), decoded.id,
                          static_cast<uint64_t>(bound->width), nullptr});
    plan.generatedAccumulators.emplace_back();
    if (bound->width > OBELISK_RT_SCALAR_NBA_MAX_BITS &&
        bound->width <= OBELISK_RT_GENERATED_NBA_MAX_BITS)
      plan.generatedAccumulators.back() =
          ("__obelisk_aot_nba_accumulator_" + Twine(root)).str();
    auto appendSites = [&](DenseI64ArrayAttr ids, uint32_t storage) {
      for (int64_t id : ids.asArrayRef()) {
        if (id < 0)
          return failure();
        plan.sites.push_back({static_cast<uint64_t>(id), root, storage});
      }
      return success();
    };
    if (failed(
            appendSites(commit.getSlots(), OBELISK_RT_STATIC_NBA_FIXED_SLOT)) ||
        failed(appendSites(commit.getAccumulatorSites(),
                           OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR)))
      return module.emitError("static NBA site identity is negative"),
             failure();
  }
  llvm::sort(plan.sites, [](const auto &left, const auto &right) {
    return left.site < right.site;
  });
  if (std::adjacent_find(plan.sites.begin(), plan.sites.end(),
                         [](const auto &left, const auto &right) {
                           return left.site == right.site;
                         }) != plan.sites.end())
    return module.emitError("static NBA site identity is duplicated"),
           failure();
  for (const obelisk_rt_static_nba_site &site : plan.sites)
    plan.siteRoots.try_emplace(site.site, site.root);
  return plan;
}

} // namespace obelisk::detail
