//===- NativeStateLayoutAnalysis.cpp - Stable native state layout -------===//

#include "obelisk/Analysis/NativeStateLayoutAnalysis.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Runtime/StableHandle.h"

#include "llvm/ADT/DenseSet.h"
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
    SmallVector<sim::ManagedHandleSlot, 2> managedRootSlots;
    if (!sim::getManagedHandleSlots(type, managedRootSlots))
      return failure();
    for (const sim::ManagedHandleSlot &slot : managedRootSlots)
      managedRootOffsets.push_back(slot.bitOffset);
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
                             std::move(managedRootOffsets),
                             std::move(managedRootSlots)});
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
      unsigned netWidth = *getSimulationStorageBitWidth(declaration.getType());
      SmallVector<std::optional<std::array<uint64_t, 3>>> propagationDelays(
          netWidth);
      if (auto delays = declaration.getPropagationDelays()) {
        ArrayRef<int64_t> values = *delays;
        for (unsigned bit = 0; bit != netWidth; ++bit) {
          size_t index = values.size() == 3 ? 0 : size_t{bit} * 3;
          if (values[index] == -1)
            continue;
          uint64_t turnoffOrDecay =
              values[index + 2] == -1
                  ? std::numeric_limits<uint64_t>::max()
                  : static_cast<uint64_t>(values[index + 2]);
          propagationDelays[bit] = std::array<uint64_t, 3>{
              static_cast<uint64_t>(values[index]),
              static_cast<uint64_t>(values[index + 1]), turnoffOrDecay};
        }
      }
      layout.netLayouts.push_back(
          {declaration.getId(), nextHandleID - 1, offset, netWidth,
           containsFourStateLogic(declaration.getType()),
           declaration.getResolutionKind(),
           declaration.getResolutionKind() == sim::NetResolutionKind::TriReg
               ? std::optional<sim::Strength>(
                     declaration.getChargeStrength().value_or(
                         sim::Strength::Medium))
               : std::nullopt,
           propagationDelays});
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
      std::optional<uint64_t> strengthGroup;
      std::optional<unsigned> strengthBank;
      if (auto group = declaration->getAttrOfType<IntegerAttr>(
              "obelisk_sim.strength_group")) {
        if (group.getValue().isNegative() ||
            group.getValue().getActiveBits() > 64) {
          declaration.emitError("driver strength group is not unsigned");
          return WalkResult::interrupt();
        }
        strengthGroup = group.getValue().getZExtValue();
      }
      if (auto bank = declaration->getAttrOfType<IntegerAttr>(
              "obelisk_sim.strength_bank")) {
        if (bank.getValue().isNegative() ||
            bank.getValue().getActiveBits() > 1) {
          declaration.emitError("driver strength bank must be zero or one");
          return WalkResult::interrupt();
        }
        strengthBank = bank.getValue().getZExtValue();
      }
      if (strengthGroup.has_value() != strengthBank.has_value()) {
        declaration.emitError(
            "driver strength group and bank must appear together");
        return WalkResult::interrupt();
      }
      layout.drivers[declaration.getId()] = handle;
      layout.driverOffsets[declaration.getId()] = offset;
      layout.driverLayouts.push_back(
          {declaration.getId(), declaration.getNetId(), nextHandleID - 1,
           offset, *width, static_cast<unsigned>(drivenLow),
           static_cast<unsigned>(drivenWidth), declaration.getStrength0(),
           declaration.getStrength1(), strengthGroup, strengthBank});
    } else if (isa<sim::SimPassSwitchDeclOp>(operation)) {
      layout.hasPassSwitch = true;
    }
    return WalkResult::advance();
  });
  if (walked.wasInterrupted())
    return failure();

  DenseMap<uint64_t, std::array<const Driver *, 2>> strengthGroups;
  for (const Driver &driver : layout.driverLayouts) {
    if (!driver.strengthGroup)
      continue;
    auto &banks = strengthGroups[*driver.strengthGroup];
    unsigned bank = *driver.strengthBank;
    if (banks[bank]) {
      module.emitError("driver strength group contains a duplicate bank");
      return failure();
    }
    banks[bank] = &driver;
  }
  for (const auto &entry : strengthGroups) {
    const Driver *low = entry.second[0];
    const Driver *high = entry.second[1];
    if (!low || !high || low->netId != high->netId ||
        low->width != high->width || low->drivenLow != high->drivenLow ||
        low->drivenWidth != high->drivenWidth ||
        low->strength1 != sim::Strength::HighZ ||
        high->strength0 != sim::Strength::HighZ) {
      module.emitError("driver strength group is not a complementary L/H pair");
      return failure();
    }
  }
  // The bytecode ABI marks only the high bank and identifies its low bank by
  // the immediately preceding record. Keep that compact representation
  // unambiguous for every layout accepted by this shared analysis.
  for (size_t index = 0; index != layout.driverLayouts.size(); ++index) {
    const Driver &driver = layout.driverLayouts[index];
    if (!driver.strengthBank)
      continue;
    bool adjacent = false;
    if (*driver.strengthBank == 0 && index + 1 < layout.driverLayouts.size()) {
      const Driver &next = layout.driverLayouts[index + 1];
      adjacent =
          next.strengthBank == 1 && next.strengthGroup == driver.strengthGroup;
    } else if (*driver.strengthBank == 1 && index != 0) {
      const Driver &previous = layout.driverLayouts[index - 1];
      adjacent = previous.strengthBank == 0 &&
                 previous.strengthGroup == driver.strengthGroup;
    }
    if (!adjacent) {
      module.emitError(
          "complementary driver strength banks must be adjacent low/high");
      return failure();
    }
  }

  SmallVector<sim::SimDesignOp> designs;
  module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
  if (designs.size() > 1) {
    module.emitError("native layout requires at most one simulation design");
    return failure();
  }
  if (!designs.empty()) {
    NetConnectivityAnalysis connectivity(designs.front());
    DenseMap<uint64_t, SmallVector<std::optional<std::array<uint64_t, 3>>>>
        declaredDelays;
    DenseMap<uint64_t, sim::NetResolutionKind> declaredResolutions;
    DenseMap<uint64_t, Net *> netsByID;
    for (Net &net : layout.netLayouts) {
      declaredDelays[net.id] = net.propagationDelays;
      declaredResolutions[net.id] = net.resolution;
      netsByID[net.id] = &net;
    }
    for (NetBit bit : connectivity.getConnectedBits()) {
      ArrayRef<NetBit> component = connectivity.getComponent(bit);
      bool componentHasDelay = llvm::any_of(component, [&](NetBit member) {
        auto declaration = declaredDelays.find(member.net);
        return declaration != declaredDelays.end() &&
               member.offset < declaration->second.size() &&
               declaration->second[member.offset].has_value();
      });
      NetDominance dominance = connectivity.getDominance(bit);
      ArrayRef<NetBit> dominatingBits = connectivity.getDominatingBits(bit);
      if (componentHasDelay &&
          (dominance.kind == NetDominanceKind::Incomplete ||
           dominatingBits.empty())) {
        module.emitError()
            << (dominance.kind == NetDominanceKind::Incomplete
                    ? "delayed collapsed net is missing port-dominance "
                      "direction"
                    : "delayed collapsed net has ambiguous port dominance");
        return failure();
      }
      auto getDelay =
          [&](NetBit member) -> std::optional<std::array<uint64_t, 3>> {
        auto declaration = declaredDelays.find(member.net);
        return declaration != declaredDelays.end() &&
                       member.offset < declaration->second.size()
                   ? declaration->second[member.offset]
                   : std::nullopt;
      };
      if (componentHasDelay && llvm::any_of(dominatingBits, [&](NetBit member) {
            return getDelay(member) != getDelay(dominatingBits.front());
          })) {
        module.emitError(
            "delayed collapsed net has ambiguous dominating delays");
        return failure();
      }
      NetBit effective =
          dominatingBits.empty() ? dominance.bit : dominatingBits.front();
      auto dominating = declaredDelays.find(effective.net);
      auto net = netsByID.find(bit.net);
      if (net == netsByID.end() || bit.offset >= net->second->width)
        return failure();
      net->second->propagationDelays[bit.offset] =
          dominating != declaredDelays.end() &&
                  effective.offset < dominating->second.size()
              ? dominating->second[effective.offset]
              : std::nullopt;
    }

    DenseSet<std::pair<uint64_t, uint64_t>> checkedMixedComponents;
    for (NetBit bit : connectivity.getConnectedBits()) {
      ArrayRef<NetBit> component = connectivity.getComponent(bit);
      if (component.empty())
        continue;
      std::pair<uint64_t, uint64_t> canonical{component.front().net,
                                              component.front().offset};
      if (!checkedMixedComponents.insert(canonical).second)
        continue;
      auto category = [&](NetBit member) {
        sim::NetResolutionKind kind = declaredResolutions.lookup(member.net);
        return kind == sim::NetResolutionKind::Tri
                   ? sim::NetResolutionKind::Wire
                   : kind;
      };
      sim::NetResolutionKind first = category(component.front());
      bool mixed = llvm::any_of(
          component, [&](NetBit member) { return category(member) != first; });
      if (!mixed)
        continue;
      NetDominance dominance = connectivity.getDominance(bit);
      ArrayRef<NetBit> dominatingBits = connectivity.getDominatingBits(bit);
      if (dominance.kind == NetDominanceKind::Incomplete ||
          dominatingBits.empty()) {
        module.emitError()
            << (dominance.kind == NetDominanceKind::Incomplete
                    ? "mixed collapsed net is missing port-dominance direction"
                    : "mixed collapsed net has ambiguous port dominance");
        return failure();
      }
      sim::NetResolutionKind effective = category(dominatingBits.front());
      if (llvm::any_of(dominatingBits, [&](NetBit member) {
            return category(member) != effective;
          })) {
        module.emitError(
            "mixed collapsed net has ambiguous dominant resolution kinds");
        return failure();
      }
    }

    for (NetBit bit : connectivity.getConnectedBits()) {
      ArrayRef<NetBit> component = connectivity.getComponent(bit);
      if (component.size() <= 1)
        continue;
      std::pair<uint64_t, uint64_t> key{bit.net, bit.offset};
      std::pair<uint64_t, uint64_t> canonical{component.front().net,
                                              component.front().offset};
      layout.connectivityCanonical[key] = canonical;
      if (key == canonical) {
        llvm::append_range(layout.connectivityComponents[canonical], component);
        ArrayRef<NetBit> dominatingBits = connectivity.getDominatingBits(bit);
        NetBit effective =
            dominatingBits.empty() ? component.front() : dominatingBits.front();
        layout.connectivityResolutions[canonical] =
            declaredResolutions.lookup(effective.net);
      }
    }

    DenseMap<std::pair<uint64_t, uint64_t>, uint64_t> uwireDrivers;
    for (const Driver &driver : layout.driverLayouts) {
      if (driver.strengthBank == 1)
        continue;
      for (uint64_t bit = driver.drivenLow;
           bit != uint64_t{driver.drivenLow} + driver.drivenWidth; ++bit) {
        ArrayRef<NetBit> component =
            connectivity.getComponent({driver.netId, bit});
        NetBit canonical =
            component.empty() ? NetBit{driver.netId, bit} : component.front();
        sim::NetResolutionKind effective =
            declaredResolutions.lookup(driver.netId);
        auto resolution = layout.connectivityResolutions.find(
            {canonical.net, canonical.offset});
        if (resolution != layout.connectivityResolutions.end())
          effective = resolution->second;
        if (effective != sim::NetResolutionKind::UWire)
          continue;
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
