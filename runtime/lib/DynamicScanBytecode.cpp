//===- DynamicScanBytecode.cpp - Feature-local dynamic scan dispatch ----===//

#include "DesignBytecodeExecution.h"
#include "RuntimeInternal.h"

#include <algorithm>
#include <cstring>
#include <optional>

namespace obelisk::designbytecode {
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

OBELISK_RT_FEATURE_HELPER bool writeScalar(const Image &image, Frame &frame,
                                           uint32_t reg, uint64_t value) {
  if (!validRegister(frame.function, reg))
    return false;
  Layout layout = layoutAt(image, frame.function, reg);
  if ((layout.kind != OBELISK_RT_DBREG_BITS &&
       layout.kind != OBELISK_RT_DBREG_LOGIC) ||
      layout.width > 64)
    return false;
  value &= finalMask(layout.width);
  std::memcpy(frame.data + layout.offset, &value,
              static_cast<size_t>(std::min<uint64_t>(layout.size, 8)));
  if (layout.kind == OBELISK_RT_DBREG_LOGIC)
    std::memset(frame.data + layout.offset + 8, 0, 8);
  return true;
}

OBELISK_RT_FEATURE_HELPER bool
readString(const Image &image, const Frame &frame, obelisk_rt_context *context,
           uint32_t reg, obelisk_rt_string_v1 &string) {
  if (!validRegister(frame.function, reg))
    return false;
  Layout layout = layoutAt(image, frame.function, reg);
  if (layout.kind != OBELISK_RT_DBREG_STRING || layout.size != 8)
    return false;
  std::memcpy(&string, frame.data + layout.offset, sizeof(string));
  return obelisk_rt_validate_string(context, string) == OBELISK_RT_OK;
}

OBELISK_RT_FEATURE_HELPER bool writeString(const Image &image, Frame &frame,
                                           uint32_t reg,
                                           obelisk_rt_string_v1 string) {
  if (!validRegister(frame.function, reg))
    return false;
  Layout layout = layoutAt(image, frame.function, reg);
  if (layout.kind != OBELISK_RT_DBREG_STRING || layout.size != 8)
    return false;
  std::memcpy(frame.data + layout.offset, &string, sizeof(string));
  return true;
}

} // namespace

// A dynamic-scan bytecode image emits one strong call to this otherwise inert
// anchor. The ordinary dispatcher holds only a weak handler reference, so a
// design without these intrinsic IDs does not extract this archive member.
extern "C" OBELISK_RT_FEATURE_TEXT void
obelisk_rt_v1_dynamic_scan_link_anchor() {}

// This is deliberately a separate, last-linked service object. Designs that
// do not encode dynamic scan intrinsics retain the established bytecode
// interpreter body and hot runtime object layout.
OBELISK_RT_FEATURE_TEXT obelisk_rt_status invokeDynamicScanIntrinsic(
    const Image &image, Frame &frame, obelisk_rt_context *context,
    IntrinsicSite site, uint32_t intrinsicId) {
  auto inputRegister = [&](uint32_t index) OBELISK_RT_FEATURE_HELPER {
    return operandAt(image, site.firstOperand + index).second;
  };
  auto outputRegister = [&](uint32_t index) OBELISK_RT_FEATURE_HELPER {
    return operandAt(image, site.firstOperand + site.inputCount + index).first;
  };
  auto scalar = [&](uint32_t index) OBELISK_RT_FEATURE_HELPER {
    return readScalar(image, frame, inputRegister(index));
  };
  auto sentinel = [&](uint32_t index,
                      uint64_t value) OBELISK_RT_FEATURE_HELPER {
    return writeScalar(image, frame, outputRegister(index), value)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_BYTECODE;
  };

  switch (intrinsicId) {
  case OBELISK_RT_INTRINSIC_V1_SCAN_DYNAMIC_VALIDATE: {
    obelisk_rt_string_v1 format = 0;
    auto planCursor = scalar(1), file = scalar(2), finalize = scalar(3),
         allowed = scalar(4);
    if (!readString(image, frame, context, inputRegister(0), format) ||
        !planCursor || !file || !finalize || !allowed ||
        *planCursor > UINT32_MAX || *file > 1 || *finalize > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t nextPlanCursor = 0;
    obelisk_rt_status status = obelisk_rt_v1_scan_dynamic_validate(
        context, format, static_cast<uint32_t>(*planCursor),
        static_cast<uint32_t>(*file), static_cast<uint32_t>(*finalize),
        *allowed, &nextPlanCursor);
    return status == OBELISK_RT_OK ? sentinel(0, nextPlanCursor) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_SCAN_DYNAMIC: {
    obelisk_rt_string_v1 input = 0;
    obelisk_rt_string_v1 format = 0;
    auto cursor = scalar(1), planCursor = scalar(3), enabled = scalar(4),
         finalize = scalar(5), allowed = scalar(6), rawTwoState = scalar(7),
         rawFourState = scalar(8);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(image, frame, context, inputRegister(0), input) ||
        !readString(image, frame, context, inputRegister(2), format) ||
        !cursor || !planCursor || !enabled || !finalize || !allowed ||
        !rawTwoState || !rawFourState ||
        *cursor > UINT32_MAX || *planCursor > UINT32_MAX || *enabled > 1 ||
        *finalize > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 field = 0;
    uint32_t nextCursor = 0, nextPlanCursor = 0, specifier = 0, ok = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_scan_dynamic(
        context, lane, input, static_cast<uint32_t>(*cursor), format,
        static_cast<uint32_t>(*planCursor), static_cast<uint32_t>(*enabled),
        static_cast<uint32_t>(*finalize), *allowed, *rawTwoState,
        *rawFourState, &field, &nextCursor, &nextPlanCursor, &specifier, &ok);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(image, frame, outputRegister(0), field))
      return OBELISK_RT_INVALID_BYTECODE;
    status = sentinel(1, nextCursor);
    if (status == OBELISK_RT_OK)
      status = sentinel(2, nextPlanCursor);
    if (status == OBELISK_RT_OK)
      status = sentinel(3, specifier);
    return status == OBELISK_RT_OK ? sentinel(4, ok) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_SCAN_DYNAMIC: {
    obelisk_rt_string_v1 format = 0;
    auto descriptor = scalar(0), planCursor = scalar(2), enabled = scalar(3),
         finalize = scalar(4), allowed = scalar(5), rawTwoState = scalar(6),
         rawFourState = scalar(7);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(image, frame, context, inputRegister(1), format) ||
        !descriptor || !planCursor || !enabled || !finalize || !allowed ||
        !rawTwoState || !rawFourState ||
        *descriptor > UINT32_MAX || *planCursor > UINT32_MAX || *enabled > 1 ||
        *finalize > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 field = 0;
    uint32_t nextPlanCursor = 0, specifier = 0, ok = 0, eof = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_scan_dynamic(
        context, lane, static_cast<uint32_t>(*descriptor), format,
        static_cast<uint32_t>(*planCursor), static_cast<uint32_t>(*enabled),
        static_cast<uint32_t>(*finalize), *allowed, *rawTwoState,
        *rawFourState, &field, &nextPlanCursor, &specifier, &ok, &eof);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(image, frame, outputRegister(0), field))
      return OBELISK_RT_INVALID_BYTECODE;
    status = sentinel(1, nextPlanCursor);
    if (status == OBELISK_RT_OK)
      status = sentinel(2, specifier);
    if (status == OBELISK_RT_OK)
      status = sentinel(3, ok);
    return status == OBELISK_RT_OK ? sentinel(4, eof) : status;
  }
  default:
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

} // namespace obelisk::designbytecode
