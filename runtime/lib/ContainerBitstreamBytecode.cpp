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

struct ByteSpan {
  const uint8_t *data;
  uint64_t size;
};

OBELISK_RT_FEATURE_HELPER std::optional<ByteSpan>
readBytes(const Image &image, const Frame &frame, uint32_t reg) {
  if (!validRegister(frame.function, reg))
    return std::nullopt;
  Layout layout = layoutAt(image, frame.function, reg);
  if (layout.kind != OBELISK_RT_DBREG_BYTES || layout.size != 16)
    return std::nullopt;
  uint64_t offset = read64(frame.data + layout.offset);
  uint64_t size = read64(frame.data + layout.offset + 8);
  if (offset > image.constantSize || size > image.constantSize - offset)
    return std::nullopt;
  return ByteSpan{image.data + image.constants + offset, size};
}

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
  bool container =
      intrinsicId == OBELISK_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM;
  bool aggregate =
      intrinsicId == OBELISK_RT_INTRINSIC_V1_AGGREGATE_EXPORT_BITSTREAM;
  if ((!container && !aggregate) || site.outputCount != 1 ||
      site.inputCount != (container ? 8u : 2u))
    return OBELISK_RT_INVALID_BYTECODE;
  auto inputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + index).second;
  };
  auto outputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + site.inputCount + index).first;
  };
  if (aggregate) {
    if (inputRegister(0) == outputRegister(0) ||
        !validRegister(frame.function, inputRegister(0)) ||
        !validRegister(frame.function, outputRegister(0)))
      return OBELISK_RT_INVALID_BYTECODE;
    std::optional<ByteSpan> plan = readBytes(image, frame, inputRegister(1));
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    bool inputFourState = input.kind == OBELISK_RT_DBREG_LOGIC;
    bool outputFourState = output.kind == OBELISK_RT_DBREG_LOGIC;
    if (!plan || (input.kind != OBELISK_RT_DBREG_BITS && !inputFourState) ||
        (output.kind != OBELISK_RT_DBREG_BITS && !outputFourState) ||
        (inputFourState && (input.size & 1)) ||
        (outputFourState && (output.size & 1)))
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t inputPlaneSize = inputFourState ? input.size / 2 : input.size;
    uint64_t outputPlaneSize = outputFourState ? output.size / 2 : output.size;
    uint8_t *inputValue = frame.data + input.offset;
    uint8_t *outputValue = frame.data + output.offset;
    return obelisk_rt_v1_aggregate_export_bitstream(
        inputValue, inputFourState ? inputValue + inputPlaneSize : nullptr,
        inputPlaneSize, input.width, inputFourState, outputValue,
        outputFourState ? outputValue + outputPlaneSize : nullptr,
        outputPlaneSize, output.width, outputFourState, plan->data, plan->size);
  }
  obelisk_rt_object_v1 *containerValue =
      readManaged(image, frame, inputRegister(0));
  std::array<std::optional<uint64_t>, 7> inputs;
  for (uint32_t index = 1; index != 8; ++index)
    inputs[index - 1] = readScalar(image, frame, inputRegister(index));
  if (!containerValue ||
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
      containerValue, value, unknown, *inputs[0], *inputs[1],
      static_cast<uint32_t>(*inputs[2]), *inputs[3], *inputs[4], *inputs[5],
      static_cast<uint32_t>(*inputs[6]));
}

} // namespace obelisk::designbytecode
