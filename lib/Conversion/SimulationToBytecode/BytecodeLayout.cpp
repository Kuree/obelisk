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
  } else if (isa<sim::TimeType, sim::CovergroupHandleType>(type)) {
    layout.kind = Bits;
    layout.width = 64;
  } else if (isa<sim::ControlType>(type)) {
    layout.kind = Bits;
    layout.width = 64;
  } else if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
                 sim::ProcessType, sim::ContextType, sim::ObserverType,
                 runtime::ContextType>(type)) {
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

  for (const auto &net : analyzed->netLayouts)
    result.netLayouts.push_back(
        {net.id, net.offset, net.width, net.fourState, net.resolution});
  for (const auto &driver : analyzed->driverLayouts) {
    auto net = llvm::find_if(analyzed->netLayouts, [&](const auto &candidate) {
      return candidate.id == driver.netId;
    });
    if (net == analyzed->netLayouts.end())
      return module.emitError("analyzed driver references an unknown net"),
             failure();
    result.driverLayouts.push_back({driver.id, driver.offset, net->offset,
                                    driver.width, driver.drivenLow,
                                    driver.drivenWidth, net->resolution});
  }

  // The net each endpoint belongs to travels with the resolutions: a run may
  // only coalesce bits that stay inside the same pair of nets. Two unrelated
  // net pairs can land adjacent in the state layout with matching stride, and
  // merging across that boundary emits a record no net contains.
  using ScalarConnection =
      std::tuple<sim::NetResolutionKind, sim::NetResolutionKind, uint64_t,
                 uint64_t>;
  std::map<std::pair<uint64_t, uint64_t>, ScalarConnection> scalarConnections;
  for (sim::SimNetConnectDeclOp connection :
       design.getBody().getOps<sim::SimNetConnectDeclOp>()) {
    auto lhs = llvm::find_if(result.netLayouts, [&](const auto &layout) {
      return layout.id == connection.getLhsNetId();
    });
    auto rhs = llvm::find_if(result.netLayouts, [&](const auto &layout) {
      return layout.id == connection.getRhsNetId();
    });
    if (lhs == result.netLayouts.end() || rhs == result.netLayouts.end())
      return connection.emitOpError("references an unknown bytecode net"),
             failure();
    for (uint64_t bit = 0; bit != connection.getWidth(); ++bit) {
      uint64_t lhsBit = lhs->offset + connection.getLhsOffset() + bit;
      uint64_t rhsBit = rhs->offset + (connection.getRhsReversed()
                                           ? connection.getRhsOffset() - bit
                                           : connection.getRhsOffset() + bit);
      sim::NetResolutionKind lhsResolution = lhs->resolution;
      sim::NetResolutionKind rhsResolution = rhs->resolution;
      uint64_t lhsNet = lhs->id, rhsNet = rhs->id;
      if (rhsBit < lhsBit) {
        std::swap(lhsBit, rhsBit);
        std::swap(lhsResolution, rhsResolution);
        std::swap(lhsNet, rhsNet);
      }
      if (lhsBit == rhsBit)
        continue;
      auto [found, inserted] = scalarConnections.try_emplace(
          std::pair{lhsBit, rhsBit},
          ScalarConnection{lhsResolution, rhsResolution, lhsNet, rhsNet});
      if (!inserted &&
          found->second !=
              ScalarConnection{lhsResolution, rhsResolution, lhsNet, rhsNet})
        return connection.emitOpError(
                   "has inconsistent duplicate scalar connectivity"),
               failure();
    }
  }
  for (auto scalar = scalarConnections.begin();
       scalar != scalarConnections.end();) {
    auto [lhsOffset, rhsOffset] = scalar->first;
    auto [lhsResolution, rhsResolution, lhsNet, rhsNet] = scalar->second;
    uint64_t width = 1;
    int direction = 0;
    auto next = std::next(scalar);
    while (next != scalarConnections.end()) {
      if (next->second != scalar->second ||
          next->first.first != lhsOffset + width)
        break;
      int candidateDirection = 0;
      if (next->first.second == rhsOffset + width)
        candidateDirection = 1;
      else if (rhsOffset >= width && next->first.second == rhsOffset - width)
        candidateDirection = -1;
      if (candidateDirection == 0 ||
          (direction != 0 && direction != candidateDirection))
        break;
      direction = candidateDirection;
      ++width;
      ++next;
    }
    result.connections.push_back({lhsOffset, rhsOffset, width, lhsResolution,
                                  rhsResolution, direction < 0});
    scalar = next;
  }

  return result;
}

} // namespace obelisk::bytecode
