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

/// Consume the dataflow certificate; this is layout validation, not another
/// reference walk. Known capture metadata alone is not a constant-handle proof.
inline std::optional<StaticNBADestination>
getStaticNBAReference(Value value, const NativeStateLayout &layout,
                      const analysis::HandleDataflowResult &analysis) {
  auto fact = analysis.facts.find(value);
  auto proof = analysis.certificates.find(value);
  if (fact == analysis.facts.end() || proof == analysis.certificates.end() ||
      !proof->second.constantAddress || fact->second.dynamic ||
      fact->second.resource != schedule::ComputeResourceKind::Storage ||
      !fact->second.descriptor)
    return std::nullopt;
  auto handle = layout.storage.find(*fact->second.descriptor);
  obelisk_rt_stable_handle_v1 decoded{};
  if (handle == layout.storage.end() ||
      !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset < 0 ||
      fact->second.low > UINT64_MAX - uint64_t(decoded.offset))
    return std::nullopt;
  return StaticNBADestination{decoded.id,
                              uint64_t(decoded.offset) + fact->second.low};
}

inline bool isNonInvalidatingStaticNBA(
    sim::SimNBAEnqueueOp op,
    const DenseMap<uint64_t, uint32_t> &staticNBASiteRoots,
    ArrayRef<obelisk_rt_static_nba_root> staticNBARoots,
    const NativeStateLayout &stateLayout,
    const analysis::HandleDataflowResult &analysis) {
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
      getStaticNBAReference(op.getDestination(), stateLayout, analysis);
  const obelisk_rt_static_nba_root &root = staticNBARoots[planned->second];
  return destination && destination->staticID == root.static_state &&
         destination->offset <= root.bit_width &&
         *width <= root.bit_width - destination->offset;
}

} // namespace obelisk::detail
#endif
