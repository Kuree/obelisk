#ifndef OBELISK_SIMULATION_TO_SCHEDULE_NATIVE_NBA_UTILS_H
#define OBELISK_SIMULATION_TO_SCHEDULE_NATIVE_NBA_UTILS_H
//===- SimulationNBAPlanning.cpp - Native static NBA plan support -------===//

#include "../SimulationToLLVMCoroutine/SimulationNBALowering.h"
#include "../SimulationToLLVMCoroutine/SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/LoopLikeInterface.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Twine.h"

#include <limits>

using namespace mlir;

namespace obelisk::detail {

/// A fixed reference proven to lower to a constant stable handle.
struct StaticNBADestination {
  uint32_t staticID;
  uint64_t offset;
};

inline std::optional<StaticNBADestination>
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

inline std::optional<StaticNBADestination>
resolveStaticNBADestination(Value value, const NativeStateLayout &layout) {
  DenseSet<Value> active;
  return resolveStaticNBADestination(value, layout, active);
}

inline bool isNonInvalidatingStaticNBA(
    sim::SimNBAEnqueueOp op,
    const DenseMap<uint64_t, uint32_t> &staticNBASiteRoots,
    ArrayRef<obelisk_rt_static_nba_root> staticNBARoots,
    const NativeStateLayout &stateLayout) {
  schedule::NBASiteAttr site = op.getSiteAttr();
  std::optional<unsigned> width = nativeStateWidth(op.getValue().getType());
  if (!site || !width || *width > 64 || op.getDelay() || site.getTiming() ||
      site.getStorage() == schedule::ComputeNBAStorageKind::DynamicFrontier)
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

} // namespace obelisk::detail
#endif
