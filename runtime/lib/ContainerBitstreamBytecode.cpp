//===- ContainerBitstreamBytecode.cpp - Feature bytecode dispatch --------===//

#include "DesignBytecodeExecution.h"
#include "RuntimeInternal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

#if defined(__clang__) || defined(__GNUC__)
extern "C" __attribute__((weak)) obelisk_rt_status
obelisk_rt_v1_recursive_export_bitstream(
    obelisk_rt_context *context, const void *inputValue,
    const void *inputUnknown, uint64_t inputPlaneSize, uint64_t inputBitWidth,
    uint32_t inputFourState, void *outValue, void *outUnknown,
    uint64_t outputPlaneSize, uint64_t outputBitWidth,
    uint32_t outputFourState, const void *plan, uint64_t planSize,
    uint32_t observe, uint32_t *outMatched, uint64_t *outWatch);
#endif

namespace obelisk::designbytecode {

#if defined(__clang__) || defined(__GNUC__)
__attribute__((weak))
#endif
obelisk_rt_status
invokeClassBitstreamIntrinsic(const Image &image, Frame &frame,
                              obelisk_rt_context *context, IntrinsicSite site,
                              uint32_t siteIndex, uint32_t intrinsicId);

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
    const Image &image, Frame &frame, obelisk_rt_context *context,
    IntrinsicSite site, uint32_t siteIndex, uint32_t intrinsicId) {
  bool container =
      intrinsicId == OBELISK_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM;
  bool aggregate =
      intrinsicId == OBELISK_RT_INTRINSIC_V1_AGGREGATE_EXPORT_BITSTREAM;
  IntrinsicSignature signature = intrinsicAt(image, site.intrinsic);
  if (container && (signature.flags == 3 || signature.flags == 4)) {
    if (!invokeClassBitstreamIntrinsic)
      return OBELISK_RT_INVALID_BYTECODE;
    return invokeClassBitstreamIntrinsic(image, frame, context, site,
                                         siteIndex, intrinsicId);
  }
  bool recursive =
      container && (signature.flags == 1 || signature.flags == 2);
  bool observeRecursive = recursive && signature.flags == 2;
  if (signature.id != intrinsicId || (!container && !aggregate) ||
      (!recursive && signature.flags != 0) ||
      (recursive && (site.inputCount != 2 || site.outputCount != 3)) ||
      (!recursive &&
       (site.outputCount != 1 ||
        site.inputCount != (container ? 8u : 2u))))
    return OBELISK_RT_INVALID_BYTECODE;
  auto inputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + index).second;
  };
  auto outputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + site.inputCount + index).first;
  };
  if (recursive) {
    if (!obelisk_rt_v1_recursive_export_bitstream)
      return OBELISK_RT_INVALID_BYTECODE;
    if (inputRegister(0) == outputRegister(0) ||
        inputRegister(0) == outputRegister(1) ||
        inputRegister(0) == outputRegister(2) ||
        outputRegister(0) == outputRegister(1) ||
        outputRegister(0) == outputRegister(2) ||
        outputRegister(1) == outputRegister(2) ||
        !validRegister(frame.function, inputRegister(0)) ||
        !validRegister(frame.function, outputRegister(0)) ||
        !validRegister(frame.function, outputRegister(1)) ||
        !validRegister(frame.function, outputRegister(2)))
      return OBELISK_RT_INVALID_BYTECODE;
    std::optional<ByteSpan> plan = readBytes(image, frame, inputRegister(1));
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    Layout matched = layoutAt(image, frame.function, outputRegister(1));
    Layout watch = layoutAt(image, frame.function, outputRegister(2));
    bool inputFourState = input.kind == OBELISK_RT_DBREG_LOGIC;
    bool outputFourState = output.kind == OBELISK_RT_DBREG_LOGIC;
    bool inputManaged = input.kind == OBELISK_RT_DBREG_MANAGED;
    bool inputString = input.kind == OBELISK_RT_DBREG_STRING;
    if (!plan ||
        (!inputManaged && !inputString && input.kind != OBELISK_RT_DBREG_BITS &&
         !inputFourState) ||
        (output.kind != OBELISK_RT_DBREG_BITS && !outputFourState) ||
        (inputFourState && (input.size & 1)) ||
        (outputFourState && (output.size & 1)) ||
        matched.kind != OBELISK_RT_DBREG_BITS || matched.width != 1 ||
        matched.size < sizeof(uint32_t) ||
        watch.kind != OBELISK_RT_DBREG_BITS || watch.width != 64 ||
        watch.size < sizeof(uint64_t) || plan->size < 32)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t inputPlaneSize = inputFourState ? input.size / 2 : input.size;
    uint64_t outputPlaneSize = outputFourState ? output.size / 2 : output.size;
    uint64_t inputWidth = read64(plan->data + 16);
    std::memset(frame.data + matched.offset, 0, matched.size);
    std::memset(frame.data + watch.offset, 0, watch.size);
    uint8_t *inputValue = frame.data + input.offset;
    uint8_t *outputValue = frame.data + output.offset;
    uint32_t matchedValue = 0;
    uint64_t watchValue = 0;
    obelisk_rt_status status = obelisk_rt_v1_recursive_export_bitstream(
        context, inputValue,
        inputFourState ? inputValue + inputPlaneSize : nullptr,
        inputPlaneSize, inputWidth, inputFourState, outputValue,
        outputFourState ? outputValue + outputPlaneSize : nullptr,
        outputPlaneSize, output.width, outputFourState, plan->data, plan->size,
        observeRecursive, &matchedValue, &watchValue);
    if (status == OBELISK_RT_OK) {
      std::memcpy(frame.data + matched.offset, &matchedValue,
                  sizeof(matchedValue));
      std::memcpy(frame.data + watch.offset, &watchValue, sizeof(watchValue));
    }
    return status;
  }
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
