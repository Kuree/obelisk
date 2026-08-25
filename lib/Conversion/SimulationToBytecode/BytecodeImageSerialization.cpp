//===- BytecodeImageSerialization.cpp - Executable image emitter -------===//
//
// Serialize planned bytecode functions, instructions, constants, state
// descriptors, and connectivity into the canonical pointer-free ABI image.
//
//===----------------------------------------------------------------------===//

#include "BytecodePlan.h"
#include "BytecodeSerialization.h"

#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Runtime/Runtime.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <cstddef>

using namespace mlir;

namespace obelisk::bytecode {

static uint32_t encodeResolution(sim::NetResolutionKind resolution,
                                 bool driver) {
  uint32_t value = static_cast<uint32_t>(resolution);
  return ((value & 3) << 1) | ((value & 12) << (driver ? 10 : 3));
}

static uint32_t encodeChargeStrength(std::optional<sim::Strength> strength) {
  if (!strength)
    return 0;
  switch (*strength) {
  case sim::Strength::Small:
    return 1;
  case sim::Strength::Medium:
    return 2;
  case sim::Strength::Large:
    return 3;
  default:
    llvm_unreachable("invalid trireg charge strength");
  }
}

SmallVector<uint8_t> serializeBytecodeImage(
    MutableArrayRef<FunctionPlan> plans, ArrayRef<Instruction> instructions,
    ArrayRef<OperandMap> operandMaps, ArrayRef<uint8_t> constants,
    ArrayRef<IntrinsicSignature> intrinsicSignatures,
    ArrayRef<IntrinsicSite> intrinsicSites,
    ArrayRef<CaptureRecord> captureRecords, const StateLayout &state) {
  using Header = obelisk_rt_design_bytecode_header_v1;
  SmallVector<uint8_t> serializedConstants(constants.begin(), constants.end());
  SmallVector<uint64_t> netDelayOffsets(state.netLayouts.size(), UINT64_MAX);
  SmallVector<bool> netDelayBitwise(state.netLayouts.size(), false);
  for (auto [index, net] : llvm::enumerate(state.netLayouts)) {
    bool anyDelay = llvm::any_of(net.propagationDelays,
                                 [](const auto &delay) { return delay; });
    if (!anyDelay)
      continue;
    alignTo(serializedConstants, 8);
    netDelayOffsets[index] = serializedConstants.size();
    bool uniform = !net.propagationDelays.empty() &&
                   net.propagationDelays.front().has_value() &&
                   llvm::all_of(net.propagationDelays, [&](const auto &delay) {
                     return delay == net.propagationDelays.front();
                   });
    netDelayBitwise[index] = !uniform;
    if (uniform) {
      for (uint64_t delay : *net.propagationDelays.front())
        append64(serializedConstants, delay);
      continue;
    }
    for (const auto &triple : net.propagationDelays)
      for (unsigned transition = 0; transition != 3; ++transition)
        append64(serializedConstants,
                 triple ? (*triple)[transition] : UINT64_MAX);
  }
  SmallVector<uint8_t> output(OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE, 0);
  static constexpr char magic[] = OBELISK_RT_DESIGN_BYTECODE_MAGIC;
  static_assert(sizeof(magic) == sizeof(Header::magic));
  std::copy(std::begin(magic), std::end(magic),
            output.begin() + offsetof(Header, magic));
  write32(output, offsetof(Header, version), OBELISK_RT_VERSION);
  write32(output, offsetof(Header, reserved), 0);
  write32(output, offsetof(Header, header_size),
          OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);

  alignTo(output, 8);
  uint64_t functionOffset = output.size();
  uint64_t layoutCursor = 0;
  uint64_t continuationCursor = 0;
  for (FunctionPlan &plan : plans) {
    append64(output, plan.stableID);
    append64(output, plan.initialScheduleRank);
    append64(output, plan.firstInstruction);
    append64(output, plan.instructionCount);
    append64(output, layoutCursor);
    append64(output, plan.layouts.size());
    append32(output, plan.function.getFunctionType().getNumInputs());
    append32(output, plan.function.getFunctionType().getNumResults());
    append64(output, plan.scratchSize);
    append64(output, plan.scratchAlignment);
    append64(output, continuationCursor);
    append64(output, plan.continuations.size());
    uint64_t functionFlags =
        (plan.function.getEntryKind() == sim::EntryKind::Function ||
         plan.function.getEntryKind() == sim::EntryKind::Observer)
            ? 0
            : (plan.frame->getFrameSize() << 1) |
                  OBELISK_RT_DESIGN_FUNCTION_PROCESS;
    if (plan.function.getEntryKind() == sim::EntryKind::Final)
      functionFlags |= OBELISK_RT_DESIGN_FUNCTION_FINAL;
    if ((functionFlags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0) {
      uint32_t homeRegion =
          getRuntimeEventRegion(plan.function.getHomeRegion());
      if (homeRegion == UINT32_MAX) {
        plan.function.emitOpError("has no executable runtime home region");
        return {};
      }
      functionFlags |= OBELISK_RT_DESIGN_FUNCTION_HOME(homeRegion);
    }
    append64(output, functionFlags);
    layoutCursor += plan.layouts.size();
    continuationCursor += plan.continuations.size();
  }
  alignTo(output, 8);
  uint64_t layoutOffset = output.size();
  for (const FunctionPlan &plan : plans)
    for (const Layout &layout : plan.layouts) {
      output.push_back(layout.kind);
      output.push_back(layout.flags);
      append16(output, 0);
      append32(output, layout.width);
      append64(output, layout.offset);
      append64(output, layout.size);
      append64(output, layout.auxiliary);
      append64(output, 0);
    }
  alignTo(output, 8);
  uint64_t codeOffset = output.size();
  for (const Instruction &instruction : instructions) {
    append16(output, instruction.opcode);
    append16(output, instruction.flags);
    append32(output, instruction.destination);
    append32(output, instruction.source0);
    append32(output, instruction.source1);
    append32(output, instruction.source2);
    append32(output, instruction.auxiliary);
    append64(output, instruction.immediate);
  }
  alignTo(output, 8);
  uint64_t operandOffset = output.size();
  for (const OperandMap &operand : operandMaps) {
    append32(output, operand.destination);
    append32(output, operand.source);
  }
  alignTo(output, 8);
  uint64_t constantOffset = output.size();
  llvm::append_range(output, serializedConstants);
  alignTo(output, 8);
  uint64_t continuationOffset = output.size();
  for (const FunctionPlan &plan : plans)
    for (const Continuation &continuation : plan.continuations) {
      append32(output, continuation.function);
      append32(output, continuation.id);
      append64(output, continuation.instruction);
      append32(output, continuation.scheduleRank);
      append32(output, 0);
    }
  alignTo(output, 8);
  uint64_t intrinsicOffset = output.size();
  for (const IntrinsicSignature &signature : intrinsicSignatures) {
    append32(output, signature.id);
    append32(output, signature.inputCount);
    append32(output, signature.outputCount);
    append32(output, signature.flags);
  }
  alignTo(output, 8);
  uint64_t siteOffset = output.size();
  for (const IntrinsicSite &site : intrinsicSites) {
    append32(output, site.intrinsic);
    append32(output, site.firstOperand);
    append32(output, site.inputCount);
    append32(output, site.outputCount);
  }
  alignTo(output, 8);
  uint64_t stateOffset = output.size();
  for (const CaptureRecord &capture : captureRecords) {
    append32(output, capture.function);
    append32(output, capture.argument);
    append64(output, capture.valueOffset);
    append64(output, capture.unknownOffset);
    append64(output, capture.planeSize);
  }
  // Static net descriptors precede drivers. They let a design-bound context
  // reproduce the native initial Z state even when a net has no drivers.
  for (auto [index, net] : llvm::enumerate(state.netLayouts)) {
    append32(output, UINT32_MAX - 1);
    bool delayed = netDelayOffsets[index] != UINT64_MAX;
    append32(output, (net.fourState ? 1u : 0u) |
                         encodeResolution(net.resolution, false) |
                         (delayed ? uint32_t{1} << 3 : 0) |
                         (netDelayBitwise[index] ? uint32_t{1} << 4 : 0) |
                         (encodeChargeStrength(net.chargeStrength) << 7));
    append64(output, net.offset);
    append64(output, netDelayOffsets[index]);
    append64(output, net.width);
  }
  for (const StateLayout::Driver &driver : state.driverLayouts) {
    append32(output, UINT32_MAX);
    // Driver planes remain four-state even when the logical destination is
    // two-state, so Z release and contention are never inferred from a
    // previously published net value.
    // Zero strength codes remain reserved for legacy/default-strong records;
    // explicit codes are one greater than the Figure 28-2 scale position.
    uint32_t strength0 = static_cast<uint32_t>(driver.strength0) + 1;
    uint32_t strength1 = static_cast<uint32_t>(driver.strength1) + 1;
    append32(output, 1u | encodeResolution(driver.resolution, true) |
                         (strength0 << 3) | (strength1 << 7) |
                         (driver.strengthBank == 1 ? uint32_t{1} << 11 : 0));
    append64(output, driver.offset + driver.drivenLow);
    append64(output, driver.netOffset + driver.drivenLow);
    append64(output, driver.drivenWidth);
  }
  alignTo(output, 8);
  uint64_t connectivityOffset = output.size();
  for (const StateLayout::Connection &connection : state.connections) {
    append64(output, connection.lhsOffset);
    append64(output, connection.rhsOffset);
    append64(output, connection.width);
    output.push_back(static_cast<uint8_t>(connection.lhsResolution));
    output.push_back(static_cast<uint8_t>(connection.rhsResolution));
    output.push_back(
        (connection.rhsReversed ? 1 : 0) | (connection.hasDominance ? 2 : 0) |
        (connection.rhsDominates ? 4 : 0) | (connection.passSwitchId ? 8 : 0) |
        (connection.passResistive ? 16 : 0) |
        (connection.passControlled ? 32 : 0));
    output.push_back(0);
    append32(output, connection.passSwitchId);
  }

  write64(output, offsetof(Header, image_size), output.size());
  write64(output, offsetof(Header, function_offset), functionOffset);
  write64(output, offsetof(Header, function_count), plans.size());
  write64(output, offsetof(Header, layout_offset), layoutOffset);
  write64(output, offsetof(Header, layout_count), layoutCursor);
  write64(output, offsetof(Header, code_offset), codeOffset);
  write64(output, offsetof(Header, instruction_count), instructions.size());
  write64(output, offsetof(Header, operand_offset), operandOffset);
  write64(output, offsetof(Header, operand_count), operandMaps.size());
  write64(output, offsetof(Header, constant_offset), constantOffset);
  write64(output, offsetof(Header, constant_size), serializedConstants.size());
  write64(output, offsetof(Header, continuation_offset), continuationOffset);
  write64(output, offsetof(Header, continuation_count), continuationCursor);
  write64(output, offsetof(Header, intrinsic_offset), intrinsicOffset);
  write64(output, offsetof(Header, intrinsic_count),
          intrinsicSignatures.size());
  write64(output, offsetof(Header, site_offset), siteOffset);
  write64(output, offsetof(Header, site_count), intrinsicSites.size());
  write64(output, offsetof(Header, state_offset), stateOffset);
  write64(output, offsetof(Header, state_count),
          captureRecords.size() + state.netLayouts.size() +
              state.driverLayouts.size());
  write64(output, offsetof(Header, connectivity_offset), connectivityOffset);
  write64(output, offsetof(Header, connectivity_count),
          state.connections.size());
  write64(output, offsetof(Header, tail_reserved), 0);
  write64(output, offsetof(Header, checksum),
          checksum(output, offsetof(Header, checksum)));
  return output;
}

} // namespace obelisk::bytecode
