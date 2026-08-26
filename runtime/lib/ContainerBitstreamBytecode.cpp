//===- ContainerBitstreamBytecode.cpp - Feature bytecode dispatch --------===//

#include "DesignBytecodeExecution.h"
#include "RuntimeInternal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace obelisk::designbytecode {

extern "C" OBELISK_RT_FEATURE_TEXT void
obelisk_rt_v1_container_bitstream_link_anchor() {}

namespace {

OBELISK_RT_FEATURE_HELPER std::optional<uint64_t>
readScalar(const Image &image, const Frame &frame, uint32_t reg) {
  if (!validRegister(frame.function, reg))
    return std::nullopt;
  Layout layout = layoutAt(image, frame.function, reg);
  if ((layout.kind != OBELISK_RT_DBREG_BITS &&
       layout.kind != OBELISK_RT_DBREG_LOGIC) ||
      layout.width > 64)
    return std::nullopt;
  uint64_t value = 0;
  std::memcpy(&value, frame.data + layout.offset,
              static_cast<size_t>(std::min<uint64_t>(layout.size, 8)));
  if (layout.kind == OBELISK_RT_DBREG_LOGIC) {
    uint64_t unknown = 0;
    std::memcpy(&unknown, frame.data + layout.offset + 8, sizeof(unknown));
    if (unknown != 0)
      return std::nullopt;
  }
  return value & finalMask(layout.width);
}

OBELISK_RT_FEATURE_HELPER obelisk_rt_object_v1 *
readManaged(const Image &image, const Frame &frame, uint32_t reg) {
  if (!validRegister(frame.function, reg))
    return nullptr;
  Layout layout = layoutAt(image, frame.function, reg);
  if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != sizeof(void *))
    return nullptr;
  obelisk_rt_object_v1 *value = nullptr;
  std::memcpy(&value, frame.data + layout.offset, sizeof(value));
  return value;
}

} // namespace

OBELISK_RT_FEATURE_TEXT obelisk_rt_status invokeContainerBitstreamIntrinsic(
    const Image &image, Frame &frame, obelisk_rt_context *, IntrinsicSite site,
    uint32_t intrinsicId) {
  if (intrinsicId != OBELISK_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM ||
      site.inputCount != 8 || site.outputCount != 1)
    return OBELISK_RT_INVALID_BYTECODE;
  auto inputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + index).second;
  };
  auto outputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + site.inputCount + index).first;
  };
  obelisk_rt_object_v1 *container = readManaged(image, frame, inputRegister(0));
  std::array<std::optional<uint64_t>, 7> inputs;
  for (uint32_t index = 1; index != 8; ++index)
    inputs[index - 1] = readScalar(image, frame, inputRegister(index));
  if (!container ||
      std::any_of(inputs.begin(), inputs.end(),
                  [](const auto &value) { return !value; }) ||
      !validRegister(frame.function, outputRegister(0)))
    return OBELISK_RT_INVALID_BYTECODE;
  Layout output = layoutAt(image, frame.function, outputRegister(0));
  bool fourState = output.kind == OBELISK_RT_DBREG_LOGIC;
  uint64_t outputPlaneSize = fourState ? output.size / 2 : output.size;
  if ((output.kind != OBELISK_RT_DBREG_BITS && !fourState) ||
      *inputs[0] > outputPlaneSize || *inputs[1] != output.width ||
      *inputs[2] != fourState)
    return OBELISK_RT_INVALID_BYTECODE;
  std::memset(frame.data + output.offset, 0, output.size);
  uint8_t *value = frame.data + output.offset;
  void *unknown = fourState ? value + outputPlaneSize : nullptr;
  return obelisk_rt_v1_container_export_bitstream(
      container, value, unknown, *inputs[0], *inputs[1],
      static_cast<uint32_t>(*inputs[2]), *inputs[3], *inputs[4], *inputs[5],
      static_cast<uint32_t>(*inputs[6]));
}

} // namespace obelisk::designbytecode
