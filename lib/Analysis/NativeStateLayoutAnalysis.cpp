//===- NativeStateLayoutAnalysis.cpp - Stable native state layout -------===//

#include "obelisk/Analysis/NativeStateLayoutAnalysis.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Runtime/StableHandle.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <limits>

using namespace mlir;

namespace obelisk::analysis {
namespace {

uint64_t encodeStaticHandle(uint32_t id) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, id,
                                         0);
}

} // namespace

FailureOr<NativeStateLayoutAnalysis>
NativeStateLayoutAnalysis::compute(ModuleOp module) {
  NativeStateLayoutAnalysis layout;
  uint32_t nextHandleID = 1;
  auto allocate = [&](Type type, bool fourState, uint64_t &offset,
                      uint64_t &handle) -> LogicalResult {
    std::optional<unsigned> width = getSimulationStorageBitWidth(type);
    if (!width || *width == 0 || *width > INT32_MAX || nextHandleID == 0 ||
        nextHandleID > OBELISK_RT_STABLE_HANDLE_MAX_STATIC_ID)
      return failure();
    SmallVector<uint64_t, 2> managedRootOffsets;
    if (!sim::getManagedHandleOffsets(type, managedRootOffsets))
      return failure();
    // Keep byte-sized roots byte-aligned. Besides avoiding cross-byte packed
    // loads and masks for ordinary scalar state, this lets read-only VPI retain
    // a canonical value with one plain store. Sub-byte roots remain densely
    // packed, and managed roots retain their stronger word alignment.
    uint64_t alignment = 1;
    if (!managedRootOffsets.empty())
      alignment = 64;
    else if (*width >= 8)
      alignment = 8;
    if (alignment != 1) {
      if (layout.bitCount >
          std::numeric_limits<uint64_t>::max() - (alignment - 1))
        return failure();
      layout.bitCount = llvm::alignTo(layout.bitCount, alignment);
    }
    offset = layout.bitCount;
    if (layout.bitCount > std::numeric_limits<uint64_t>::max() - *width)
      return failure();
    layout.bitCount += *width;
    handle = encodeStaticHandle(nextHandleID);
    layout.bounds.push_back({nextHandleID++, offset, *width, fourState,
                             std::move(managedRootOffsets)});
    return success();
  };
  WalkResult walked = module.walk([&](Operation *operation) {
    if (auto declaration = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      uint64_t offset;
      uint64_t handle;
      if (failed(allocate(declaration.getType(),
                          containsFourStateLogic(declaration.getType()), offset,
                          handle))) {
        declaration.emitError("native storage must have a fixed packed width");
        return WalkResult::interrupt();
      }
      layout.storage[declaration.getId()] = handle;
      layout.storageOffsets[declaration.getId()] = offset;
    } else if (auto declaration = dyn_cast<sim::SimNetDeclOp>(operation)) {
      uint64_t offset;
      uint64_t handle;
      if (failed(allocate(declaration.getType(),
                          containsFourStateLogic(declaration.getType()), offset,
                          handle))) {
        declaration.emitError("native net must have a fixed packed width");
        return WalkResult::interrupt();
      }
      layout.nets[declaration.getId()] = handle;
      layout.netOffsets[declaration.getId()] = offset;
      layout.netLayouts.push_back(
          {declaration.getId(), nextHandleID - 1, offset,
           *getSimulationStorageBitWidth(declaration.getType()),
           containsFourStateLogic(declaration.getType()),
           declaration.getResolutionKind()});
    } else if (auto declaration = dyn_cast<sim::SimDriverDeclOp>(operation)) {
      auto found = layout.nets.find(declaration.getNetId());
      if (found == layout.nets.end()) {
        declaration.emitError("native driver references an unknown net");
        return WalkResult::interrupt();
      }
      uint64_t offset;
      uint64_t handle;
      std::optional<unsigned> width =
          getSimulationStorageBitWidth(declaration.getType());
      // Driver state always retains an unknown plane so it can represent Z
      // release even when the resolved destination is two-state.
      if (!width ||
          failed(allocate(declaration.getType(), true, offset, handle))) {
        declaration.emitError("native driver must have a fixed packed width");
        return WalkResult::interrupt();
      }
      uint64_t drivenLow =
          declaration.getDrivenLowAttr()
              ? declaration.getDrivenLowAttr().getValue().getZExtValue()
              : 0;
      uint64_t drivenWidth =
          declaration.getDrivenWidthAttr()
              ? declaration.getDrivenWidthAttr().getValue().getZExtValue()
              : *width;
      if (drivenLow > *width || drivenWidth > *width - drivenLow) {
        declaration.emitError("native driver has an invalid driven range");
        return WalkResult::interrupt();
      }
      layout.drivers[declaration.getId()] = handle;
      layout.driverOffsets[declaration.getId()] = offset;
      layout.driverLayouts.push_back(
          {declaration.getId(), declaration.getNetId(), nextHandleID - 1,
           offset, *width, static_cast<unsigned>(drivenLow),
           static_cast<unsigned>(drivenWidth)});
    }
    return WalkResult::advance();
  });
  if (walked.wasInterrupted())
    return failure();

  SmallVector<sim::SimDesignOp> designs;
  module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
  if (designs.size() > 1) {
    module.emitError("native layout requires at most one simulation design");
    return failure();
  }
  if (!designs.empty()) {
    NetConnectivityAnalysis connectivity(designs.front());
    for (const Net &net : layout.netLayouts) {
      for (uint64_t bit = 0; bit != net.width; ++bit) {
        ArrayRef<NetBit> component = connectivity.getComponent({net.id, bit});
        if (component.size() <= 1)
          continue;
        std::pair<uint64_t, uint64_t> key{net.id, bit};
        std::pair<uint64_t, uint64_t> canonical{component.front().net,
                                                component.front().offset};
        layout.connectivityCanonical[key] = canonical;
        if (key == canonical)
          llvm::append_range(layout.connectivityComponents[canonical],
                             component);
      }
    }

    DenseMap<std::pair<uint64_t, uint64_t>, uint64_t> uwireDrivers;
    for (const Driver &driver : layout.driverLayouts) {
      auto target = llvm::find_if(layout.netLayouts, [&](const auto &net) {
        return net.id == driver.netId;
      });
      if (target == layout.netLayouts.end() ||
          target->resolution != sim::NetResolutionKind::UWire)
        continue;
      for (uint64_t bit = driver.drivenLow;
           bit != uint64_t{driver.drivenLow} + driver.drivenWidth; ++bit) {
        ArrayRef<NetBit> component =
            connectivity.getComponent({driver.netId, bit});
        NetBit canonical =
            component.empty() ? NetBit{driver.netId, bit} : component.front();
        if (++uwireDrivers[{canonical.net, canonical.offset}] > 1) {
          module.emitError()
              << "uwire connectivity component " << canonical.net << "["
              << canonical.offset << "] has more than one driver";
          return failure();
        }
      }
    }
  }
  if (layout.bitCount >= OBELISK_RT_STABLE_HANDLE_STATIC_TAG) {
    module.emitError("native static state exceeds the handle address space");
    return failure();
  }
  // Keep one byte addressable so poison-free invalid-handle paths always have
  // a safe GEP base even for a design with no state.
  layout.bitCount = std::max<uint64_t>(layout.bitCount, 8);
  return layout;
}

} // namespace obelisk::analysis
