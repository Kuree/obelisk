//===- ClassBitstreamBytecode.cpp - Class cast bytecode dispatch --------===//

#include "DesignBytecodeExecution.h"
#include "RuntimeInternal.h"

#include <cstring>

namespace obelisk::designbytecode {

extern "C" OBELISK_RT_FEATURE_TEXT void
obelisk_rt_v1_class_bitstream_bytecode_link_anchor() {}

OBELISK_RT_FEATURE_TEXT obelisk_rt_status invokeClassBitstreamIntrinsic(
    const Image &image, Frame &frame, obelisk_rt_context *context,
    IntrinsicSite site, uint32_t siteIndex, uint32_t intrinsicId) {
  IntrinsicSignature signature = intrinsicAt(image, site.intrinsic);
  bool observe = signature.flags == 4;
  if (intrinsicId != OBELISK_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM ||
      signature.id != intrinsicId ||
      (signature.flags != 3 && signature.flags != 4) || site.inputCount != 1 ||
      site.outputCount != 3)
    return OBELISK_RT_INVALID_BYTECODE;
  auto inputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + index).second;
  };
  auto outputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + site.inputCount + index).first;
  };
  uint32_t inputReg = inputRegister(0);
  uint32_t outputReg = outputRegister(0);
  uint32_t matchedReg = outputRegister(1);
  uint32_t watchReg = outputRegister(2);
  if (inputReg == outputReg || inputReg == matchedReg || inputReg == watchReg ||
      outputReg == matchedReg || outputReg == watchReg ||
      matchedReg == watchReg || !validRegister(frame.function, inputReg) ||
      !validRegister(frame.function, outputReg) ||
      !validRegister(frame.function, matchedReg) ||
      !validRegister(frame.function, watchReg))
    return OBELISK_RT_INVALID_BYTECODE;
  Layout input = layoutAt(image, frame.function, inputReg);
  Layout output = layoutAt(image, frame.function, outputReg);
  Layout matched = layoutAt(image, frame.function, matchedReg);
  Layout watch = layoutAt(image, frame.function, watchReg);
  bool inputFourState = input.kind == OBELISK_RT_DBREG_LOGIC;
  bool outputFourState = output.kind == OBELISK_RT_DBREG_LOGIC;
  bool validInput = input.kind == OBELISK_RT_DBREG_BITS || inputFourState ||
                    input.kind == OBELISK_RT_DBREG_MANAGED ||
                    input.kind == OBELISK_RT_DBREG_STRING;
  if (!validInput ||
      (output.kind != OBELISK_RT_DBREG_BITS && !outputFourState) ||
      (inputFourState && (input.size & 1)) ||
      (outputFourState && (output.size & 1)) ||
      matched.kind != OBELISK_RT_DBREG_BITS || matched.width != 1 ||
      matched.size < sizeof(uint32_t) || watch.kind != OBELISK_RT_DBREG_BITS ||
      watch.width != 64 || watch.size < sizeof(uint64_t))
    return OBELISK_RT_INVALID_BYTECODE;
  uint64_t inputPhysicalPlane = inputFourState ? input.size / 2 : input.size;
  uint64_t outputPhysicalPlane =
      outputFourState ? output.size / 2 : output.size;
  uint64_t inputPlaneSize = (uint64_t{input.width} + 7) / 8;
  uint64_t outputPlaneSize = (uint64_t{output.width} + 7) / 8;
  if (inputPlaneSize > inputPhysicalPlane ||
      outputPlaneSize > outputPhysicalPlane)
    return OBELISK_RT_INVALID_BYTECODE;
  uint8_t *inputValue = frame.data + input.offset;
  uint8_t *outputValue = frame.data + output.offset;
  uint32_t matchedValue = 0;
  uint64_t watchValue = 0;
  obelisk_rt_status status = obelisk_rt_class_bitstream_export_bytecode(
      context, frame.functionIndex, siteIndex, inputValue,
      inputFourState ? inputValue + inputPhysicalPlane : nullptr,
      inputPlaneSize, input.width, inputFourState, outputValue,
      outputFourState ? outputValue + outputPhysicalPlane : nullptr,
      outputPlaneSize, output.width, outputFourState, observe, &matchedValue,
      &watchValue);
  if (status == OBELISK_RT_OK) {
    std::memset(outputValue + outputPlaneSize, 0,
                static_cast<size_t>(outputPhysicalPlane - outputPlaneSize));
    if (outputFourState)
      std::memset(outputValue + outputPhysicalPlane + outputPlaneSize, 0,
                  static_cast<size_t>(outputPhysicalPlane - outputPlaneSize));
    std::memset(frame.data + matched.offset, 0, matched.size);
    std::memcpy(frame.data + matched.offset, &matchedValue,
                sizeof(matchedValue));
    std::memset(frame.data + watch.offset, 0, watch.size);
    std::memcpy(frame.data + watch.offset, &watchValue, sizeof(watchValue));
  }
  return status;
}

} // namespace obelisk::designbytecode
