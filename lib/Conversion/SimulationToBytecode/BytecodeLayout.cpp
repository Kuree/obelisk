//===- BytecodeLayout.cpp - Bytecode register and state layout ----------===//
//
// Compute target-independent bytecode value layouts and the analyzed native
// state map consumed by both the encoder and serialized image.
//
//===----------------------------------------------------------------------===//

#include "BytecodeLayout.h"
#include "BytecodeSerialization.h"

#include "obelisk/Analysis/NativeStateLayoutAnalysis.h"
#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Dialect/Runtime/RuntimeTypes.h"

#include "mlir/IR/BuiltinOps.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <type_traits>
#include <utility>

using namespace mlir;

namespace obelisk::bytecode {

FailureOr<Layout> getLayout(Type type) {
  Layout layout;
  if (auto integer = dyn_cast<IntegerType>(type)) {
    layout.kind = Bits;
    layout.width = integer.getWidth();
    layout.flags = integer.isSigned() ? 1 : 0;
  } else if (type.isF32()) {
    layout.kind = Real32;
    layout.width = 32;
  } else if (type.isF64()) {
    layout.kind = Real64;
    layout.width = 64;
  } else if (auto logic = dyn_cast<sim::LogicType>(type)) {
    layout.kind = Logic;
    layout.width = logic.getWidth();
  } else if (isa<sim::TimeType, sim::ProcessType, sim::ManagedWatchType,
                 sim::CovergroupHandleType, sim::VirtualInterfaceType,
                 sim::ChandleType>(type)) {
    layout.kind = Bits;
    layout.width = 64;
  } else if (isa<sim::ControlType>(type)) {
    layout.kind = Bits;
    layout.width = 64;
  } else if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
                 sim::ContextType, sim::ObserverType, runtime::ContextType>(
                 type)) {
    layout.kind = Handle;
    layout.width = 256;
  } else if (isa<runtime::StatusType>(type)) {
    layout.kind = Status;
    layout.width = 64;
  } else if (isa<sim::BytesType>(type)) {
    layout.kind = Bytes;
    layout.width = 128;
  } else if (isa<sim::StringType>(type)) {
    layout.kind = String;
    layout.width = 64;
  } else if (sim::isManagedHandleType(type)) {
    layout.kind = Managed;
    layout.width = 64;
  } else if (isa<sim::ManagedRefType>(type)) {
    layout.kind = ManagedRef;
    layout.width = 128;
  } else if (isa<sim::ArgumentRefType>(type)) {
    layout.kind = ArgumentRef;
    layout.width = 192;
  } else if (std::optional<uint32_t> width = simulationWidth(type)) {
    layout.kind = containsLogic(type) ? Logic : Bits;
    layout.width = static_cast<uint32_t>(*width);
  } else {
    return failure();
  }
  uint64_t limbs = (uint64_t{layout.width} + 63) / 64;
  switch (layout.kind) {
  case Bits:
    layout.size = limbs * 8;
    break;
  case Logic:
    layout.size = limbs * 16;
    break;
  case Handle:
    layout.size = 32;
    break;
  case Status:
  case Resource:
    layout.size = 8;
    break;
  case Bytes:
    layout.size = 16;
    break;
  case Managed:
  case String:
    layout.size = 8;
    break;
  case Real32:
    layout.size = 4;
    break;
  case Real64:
    layout.size = 8;
    break;
  case ManagedRef:
    layout.size = 16;
    break;
  case ArgumentRef:
    layout.size = 24;
    break;
  default:
    return failure();
  }
  return layout;
}

FailureOr<ManagedValueStorage>
getManagedValueStorage(Type type, const llvm::DataLayout &dataLayout) {
  llvm::LLVMContext llvmContext;
  FailureOr<analysis::SimulationStorageProperties> storage =
      analysis::getSimulationStorageProperties(type, dataLayout, llvmContext);
  if (failed(storage) || storage->managedReference)
    return failure();
  return ManagedValueStorage{storage->size, storage->alignment,
                             storage->fourState};
}

FailureOr<StateLayout> buildStateLayout(sim::SimDesignOp design) {
  StateLayout result;
  ModuleOp module = design->getParentOfType<ModuleOp>();
  FailureOr<analysis::NativeStateLayoutAnalysis> analyzed =
      analysis::NativeStateLayoutAnalysis::compute(module);
  if (failed(analyzed))
    return failure();

  // Bytecode state lives in the execution context's canonical flat planes.
  // Native lowering uses the stable object handles from the same analysis,
  // while bytecode handles must retain canonical bit offsets so direct entry
  // execution does not depend on scheduler-main static-state registration.
  result.storage = analyzed->storageOffsets;
  result.nets = analyzed->netOffsets;
  result.drivers = analyzed->driverOffsets;
  result.storageOffsets = analyzed->storageOffsets;
  result.netOffsets = analyzed->netOffsets;
  result.driverOffsets = analyzed->driverOffsets;
  result.bits = analyzed->bitCount;

  llvm::DenseMap<uint64_t, size_t> netLayoutIndices;
  for (const auto &net : analyzed->netLayouts) {
    netLayoutIndices.try_emplace(net.id, result.netLayouts.size());
    result.netLayouts.push_back({net.id, net.offset, net.width, net.fourState,
                                 net.resolution, net.chargeStrength,
                                 net.propagationDelays});
  }
  for (const auto &driver : analyzed->driverLayouts) {
    auto net = netLayoutIndices.find(driver.netId);
    if (net == netLayoutIndices.end())
      return module.emitError("analyzed driver references an unknown net"),
             failure();
    const auto &netLayout = result.netLayouts[net->second];
    result.driverLayouts.push_back(
        {driver.id, driver.offset, netLayout.offset, driver.width,
         driver.drivenLow, driver.drivenWidth, netLayout.resolution,
         driver.strength0, driver.strength1, driver.strengthBank});
  }

  // The net each endpoint belongs to travels with the resolutions: a run may
  // only coalesce bits that stay inside the same pair of nets. Two unrelated
  // net pairs can land adjacent in the state layout with matching stride, and
  // merging across that boundary emits a record no net contains.
  using ScalarConnection =
      std::tuple<sim::NetResolutionKind, sim::NetResolutionKind, uint64_t,
                 uint64_t, std::optional<bool>, uint32_t, bool, bool, bool,
                 bool, bool>;
  // Keep the declaration identity separate from the runtime control ID.  An
  // elaborated primitive array can publish one shared scalar control for many
  // distinct switches, including parallel switches between the same bits.
  // The declaration ID preserves those parallel contributions while the
  // control ID lets the runtime update the complete frozen group once.
  using ScalarConnectionKey =
      std::tuple<uint64_t, uint64_t, uint32_t, uint32_t>;
  std::map<ScalarConnectionKey, ScalarConnection> scalarConnections;
  auto collectConnection = [&](auto connection, uint32_t identity,
                               uint32_t passSwitchId, bool passResistive,
                               bool passControlled, bool passDirected = false,
                               bool passDelayed = false) -> LogicalResult {
    auto lhsIndex = netLayoutIndices.find(connection.getLhsNetId());
    auto rhsIndex = netLayoutIndices.find(connection.getRhsNetId());
    if (lhsIndex == netLayoutIndices.end() ||
        rhsIndex == netLayoutIndices.end())
      return connection.emitOpError("references an unknown bytecode net"),
             failure();
    const auto &lhs = result.netLayouts[lhsIndex->second];
    const auto &rhs = result.netLayouts[rhsIndex->second];
    for (uint64_t bit = 0; bit != connection.getWidth(); ++bit) {
      uint64_t lhsBit = lhs.offset + connection.getLhsOffset() + bit;
      uint64_t rhsBit = rhs.offset + (connection.getRhsReversed()
                                          ? connection.getRhsOffset() - bit
                                          : connection.getRhsOffset() + bit);
      sim::NetResolutionKind lhsResolution = lhs.resolution;
      sim::NetResolutionKind rhsResolution = rhs.resolution;
      uint64_t lhsNet = lhs.id, rhsNet = rhs.id;
      std::optional<bool> rhsDominates;
      // Directed switch declarations use the semantic right terminal as the
      // source and the left terminal as the destination. Preserve that
      // orientation after canonical endpoint ordering.
      bool passRhsToLhs = true;
      if constexpr (std::is_same_v<decltype(connection),
                                   sim::SimNetConnectDeclOp>)
        rhsDominates = connection.getRhsDominates();
      if (rhsBit < lhsBit) {
        std::swap(lhsBit, rhsBit);
        std::swap(lhsResolution, rhsResolution);
        std::swap(lhsNet, rhsNet);
        if (rhsDominates)
          rhsDominates = !*rhsDominates;
        passRhsToLhs = false;
      }
      if (lhsBit == rhsBit)
        continue;
      auto [found, inserted] = scalarConnections.try_emplace(
          ScalarConnectionKey{lhsBit, rhsBit, identity, passSwitchId},
          ScalarConnection{lhsResolution, rhsResolution, lhsNet, rhsNet,
                           rhsDominates, passSwitchId, passResistive,
                           passControlled, passDirected, passRhsToLhs,
                           passDelayed});
      if (!inserted &&
          found->second != ScalarConnection{lhsResolution, rhsResolution,
                                            lhsNet, rhsNet, rhsDominates,
                                            passSwitchId, passResistive,
                                            passControlled, passDirected,
                                            passRhsToLhs, passDelayed})
        return connection.emitOpError(
                   "has inconsistent duplicate scalar connectivity"),
               failure();
    }
    return success();
  };
  for (sim::SimNetConnectDeclOp connection :
       design.getBody().getOps<sim::SimNetConnectDeclOp>())
    if (failed(collectConnection(connection, 0, 0, false, false, false, false)))
      return failure();
  for (sim::SimPassSwitchDeclOp connection :
       design.getBody().getOps<sim::SimPassSwitchDeclOp>()) {
    if (connection.getId() >= UINT32_MAX)
      return connection.emitOpError("ID exceeds bytecode pass-switch range"),
             failure();
    auto resistive = connection->getAttrOfType<BoolAttr>("resistive");
    auto controlled = connection->getAttrOfType<BoolAttr>("controlled");
    auto directed = connection->getAttrOfType<BoolAttr>("directed");
    auto delayed = connection->getAttrOfType<BoolAttr>("delayed");
    uint32_t controlId = static_cast<uint32_t>(connection.getId()) + 1;
    if (auto group = connection->getAttrOfType<IntegerAttr>("control_group")) {
      if (group.getValue().isNegative() ||
          group.getValue().getActiveBits() > 32 ||
          group.getValue().getZExtValue() >= UINT32_MAX)
        return connection.emitOpError(
                   "control group exceeds bytecode pass-switch range"),
               failure();
      controlId = static_cast<uint32_t>(group.getValue().getZExtValue()) + 1;
    }
    if (failed(collectConnection(
            connection, static_cast<uint32_t>(connection.getId()) + 1,
            controlId, resistive && resistive.getValue(),
            controlled && controlled.getValue(),
            directed && directed.getValue(), delayed && delayed.getValue())))
      return failure();
  }
  for (auto scalar = scalarConnections.begin();
       scalar != scalarConnections.end();) {
    auto [lhsOffset, rhsOffset, ignoredIdentity, passSwitchId] = scalar->first;
    auto [lhsResolution, rhsResolution, lhsNet, rhsNet, rhsDominates,
          ignoredPassSwitchId, passResistive, passControlled, passDirected,
          passRhsToLhs, passDelayed] = scalar->second;
    uint64_t width = 1;
    int direction = 0;
    auto next = std::next(scalar);
    while (next != scalarConnections.end()) {
      if (next->second != scalar->second ||
          std::get<0>(next->first) != lhsOffset + width)
        break;
      int candidateDirection = 0;
      if (std::get<1>(next->first) == rhsOffset + width)
        candidateDirection = 1;
      else if (rhsOffset >= width &&
               std::get<1>(next->first) == rhsOffset - width)
        candidateDirection = -1;
      if (candidateDirection == 0 ||
          (direction != 0 && direction != candidateDirection))
        break;
      direction = candidateDirection;
      ++width;
      ++next;
    }
    result.connections.push_back(
        {lhsOffset, rhsOffset, width, lhsResolution, rhsResolution,
         direction < 0, rhsDominates.has_value(), rhsDominates.value_or(false),
         passSwitchId, passResistive, passControlled, passDirected,
         passRhsToLhs, passDelayed});
    scalar = next;
  }

  return result;
}

} // namespace obelisk::bytecode
