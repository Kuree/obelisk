//===- DesignBytecodeIntrinsics.cpp - Runtime service dispatch ----------===//

#include "DesignBytecodeExecution.h"
#include "DesignBytecodeNets.h"
#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace obelisk::designbytecode {

struct ByteSpan {
  const uint8_t *data = nullptr;
  uint64_t size = 0;
};

std::optional<ByteSpan> readByteSpan(const Image &image, const Frame &frame,
                                     uint32_t reg) {
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

std::optional<uint64_t> readScalar(const Image &image, const Frame &frame,
                                   uint32_t reg) {
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

bool writeScalar(const Image &image, Frame &frame, uint32_t reg,
                 uint64_t value) {
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

uint64_t extractScalarBits(const LimbVector &plane, uint64_t first,
                           uint64_t width) {
  size_t word = static_cast<size_t>(first / 64);
  unsigned shift = static_cast<unsigned>(first % 64);
  uint64_t result = plane[word] >> shift;
  if (shift != 0 && word + 1 < plane.size())
    result |= plane[word + 1] << (64 - shift);
  return result & finalMask(width);
}

bool packBytes(const Image &image, Frame &frame, uint32_t reg,
               const uint8_t *data, uint64_t size, bool highAlignment) {
  if (!validRegister(frame.function, reg))
    return false;
  Layout layout = layoutAt(image, frame.function, reg);
  if (layout.kind != OBELISK_RT_DBREG_BITS &&
      layout.kind != OBELISK_RT_DBREG_LOGIC)
    return false;
  uint64_t capacity = (uint64_t{layout.width} + 7) / 8;
  uint64_t count = std::min(size, capacity);
  Logic result{layout.width, layout.kind == OBELISK_RT_DBREG_LOGIC,
               LimbVector(limbCount(layout.width)),
               LimbVector(limbCount(layout.width))};
  for (uint64_t index = 0; index != count; ++index) {
    uint64_t byte = highAlignment ? capacity - 1 - index : count - 1 - index;
    for (unsigned bitIndex = 0; bitIndex != 8; ++bitIndex) {
      uint64_t destination = byte * 8 + bitIndex;
      if (destination < layout.width)
        setBit(result.value, destination,
               (data[index] & (uint8_t{1} << bitIndex)) != 0);
    }
  }
  writeLogic(frame.data, layout, result);
  return true;
}

obelisk_rt_status invokeIntrinsic(const Image &image, Frame &frame,
                                  obelisk_rt_context *context,
                                  uint32_t siteIndex) {
  if (!validIntrinsic(image, frame.function, siteIndex))
    return OBELISK_RT_INVALID_BYTECODE;
  IntrinsicSite site = siteAt(image, siteIndex);
  IntrinsicSignature signature = intrinsicAt(image, site.intrinsic);
  auto inputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + index).second;
  };
  auto outputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + site.inputCount + index).first;
  };
  auto scalar = [&](uint32_t index) {
    return readScalar(image, frame, inputRegister(index));
  };
  auto bytes = [&](uint32_t index) {
    return readByteSpan(image, frame, inputRegister(index));
  };
  auto sentinel = [&](uint32_t index, uint64_t value) {
    return writeScalar(image, frame, outputRegister(index), value)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_BYTECODE;
  };
  auto realInput = [&](uint32_t index) -> std::optional<double> {
    uint32_t reg = inputRegister(index);
    if (!validRegister(frame.function, reg))
      return std::nullopt;
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_REAL64 || layout.size != sizeof(double))
      return std::nullopt;
    double value = 0.0;
    std::memcpy(&value, frame.data + layout.offset, sizeof(value));
    return value;
  };
  auto writeReal = [&](uint32_t index, double value) {
    uint32_t reg = outputRegister(index);
    if (!validRegister(frame.function, reg))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_REAL64 || layout.size != sizeof(value))
      return OBELISK_RT_INVALID_BYTECODE;
    std::memcpy(frame.data + layout.offset, &value, sizeof(value));
    return OBELISK_RT_OK;
  };
  auto writeStatus = [&](uint32_t index, obelisk_rt_status value) {
    Layout output = layoutAt(image, frame.function, outputRegister(index));
    if (output.kind != OBELISK_RT_DBREG_STATUS || output.size != 8)
      return false;
    uint64_t encoded = static_cast<uint32_t>(value);
    std::memcpy(frame.data + output.offset, &encoded, sizeof(encoded));
    return true;
  };
  auto finishVPI = [&](uint32_t statusIndex, obelisk_rt_status value) {
    return writeStatus(statusIndex, value) ? OBELISK_RT_OK
                                           : OBELISK_RT_INVALID_BYTECODE;
  };
  auto cursorInput = [&](uint32_t index, obelisk_rt_design_cursor_v1 &cursor) {
    auto encoded = scalar(index);
    if (!encoded)
      return false;
    cursor.offset = *encoded;
    return true;
  };
  auto readManaged = [&](uint32_t reg) -> obelisk_rt_object_v1 * {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8)
      return nullptr;
    obelisk_rt_object_v1 *object = nullptr;
    std::memcpy(&object, frame.data + layout.offset, sizeof(object));
    return object;
  };
  auto writeManaged = [&](uint32_t reg, obelisk_rt_object_v1 *object) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8)
      return false;
    std::memcpy(frame.data + layout.offset, &object, sizeof(object));
    return true;
  };
  auto readString = [&](uint32_t reg, obelisk_rt_string_v1 &string) -> bool {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_STRING || layout.size != 8)
      return false;
    std::memcpy(&string, frame.data + layout.offset, sizeof(string));
    return obelisk_rt_validate_string(context, string) == OBELISK_RT_OK;
  };
  auto writeString = [&](uint32_t reg, obelisk_rt_string_v1 string) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_STRING || layout.size != 8)
      return false;
    std::memcpy(frame.data + layout.offset, &string, sizeof(string));
    return true;
  };
  auto readManagedRef = [&](uint32_t reg, obelisk_rt_object_v1 *&object,
                            uint64_t &offset) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_MANAGED_REF || layout.size != 16)
      return false;
    std::memcpy(&object, frame.data + layout.offset, sizeof(object));
    std::memcpy(&offset, frame.data + layout.offset + 8, sizeof(offset));
    return true;
  };
  auto readArgumentRef = [&](uint32_t reg, obelisk_rt_object_v1 *&owner,
                             uint64_t &payload, uint32_t &managed) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_ARGUMENT_REF || layout.size != 24)
      return false;
    std::memcpy(&owner, frame.data + layout.offset, sizeof(owner));
    std::memcpy(&payload, frame.data + layout.offset + 8, sizeof(payload));
    uint64_t tag = 0;
    std::memcpy(&tag, frame.data + layout.offset + 16, sizeof(tag));
    if (tag > 2)
      return false;
    managed = static_cast<uint32_t>(tag);
    return true;
  };
  auto readAssocKey = [&](obelisk_rt_object_v1 *array, uint32_t reg,
                          obelisk_rt_assoc_key_v1 &key) {
    Layout layout = layoutAt(image, frame.function, reg);
    key = {};
    if (obelisk_rt_v1_assoc_key_info(array, &key.kind, &key.width) !=
        OBELISK_RT_OK)
      return false;
    if (layout.kind == OBELISK_RT_DBREG_STRING) {
      if (key.kind != OBELISK_RT_ASSOC_KEY_STRING || layout.size != 8)
        return false;
      std::memcpy(&key.string, frame.data + layout.offset, sizeof(key.string));
      return true;
    }
    if ((layout.kind != OBELISK_RT_DBREG_BITS &&
         layout.kind != OBELISK_RT_DBREG_LOGIC) ||
        key.kind == OBELISK_RT_ASSOC_KEY_STRING || layout.width != key.width ||
        layout.width == 0 || layout.width > 64)
      return false;
    uint64_t planeSize =
        layout.kind == OBELISK_RT_DBREG_LOGIC ? layout.size / 2 : layout.size;
    if (planeSize == 0 || planeSize > sizeof(uint64_t))
      return false;
    std::memcpy(&key.value, frame.data + layout.offset,
                static_cast<size_t>(planeSize));
    if (layout.kind == OBELISK_RT_DBREG_LOGIC)
      std::memcpy(&key.unknown, frame.data + layout.offset + planeSize,
                  static_cast<size_t>(planeSize));
    return true;
  };
  auto writeAssocKey = [&](uint32_t reg, const obelisk_rt_assoc_key_v1 &key) {
    Layout layout = layoutAt(image, frame.function, reg);
    std::memset(frame.data + layout.offset, 0, layout.size);
    if (layout.kind == OBELISK_RT_DBREG_STRING) {
      if (key.kind != OBELISK_RT_ASSOC_KEY_STRING || layout.size != 8)
        return false;
      std::memcpy(frame.data + layout.offset, &key.string, sizeof(key.string));
      return true;
    }
    if ((layout.kind != OBELISK_RT_DBREG_BITS &&
         layout.kind != OBELISK_RT_DBREG_LOGIC) ||
        key.kind == OBELISK_RT_ASSOC_KEY_STRING || layout.width != key.width)
      return false;
    uint64_t planeSize =
        layout.kind == OBELISK_RT_DBREG_LOGIC ? layout.size / 2 : layout.size;
    if (planeSize == 0 || planeSize > sizeof(uint64_t))
      return false;
    std::memcpy(frame.data + layout.offset, &key.value,
                static_cast<size_t>(planeSize));
    if (layout.kind == OBELISK_RT_DBREG_LOGIC)
      std::memcpy(frame.data + layout.offset + planeSize, &key.unknown,
                  static_cast<size_t>(planeSize));
    return true;
  };
  switch (signature.id) {
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_SIZE:
    return sentinel(
        0, obelisk_rt_v1_container_size(readManaged(inputRegister(0))));
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE_LIKE: {
    auto size = scalar(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!size)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *result = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_container_create_like(
        lane, readManaged(inputRegister(0)), readManaged(inputRegister(1)),
        *size, &result);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_READ: {
    auto index = scalar(1);
    if (!index)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    std::memset(frame.data + output.offset, 0, output.size);
    if (output.kind == OBELISK_RT_DBREG_HANDLE) {
      uint64_t event = UINT64_MAX;
      obelisk_rt_status status = obelisk_rt_v1_container_read_checked(
          readManaged(inputRegister(0)), static_cast<int64_t>(*index), &event,
          sizeof(event), nullptr, 0);
      if (status != OBELISK_RT_OK)
        return status;
      uint32_t kind = OBELISK_RT_DESCRIPTOR_EVENT;
      int64_t start = static_cast<int64_t>(event);
      int64_t begin = start == kInvalidHandleStart ? 0 : start;
      int64_t end = start == kInvalidHandleStart
                        ? 0
                        : (start == INT64_MAX ? start : start + 1);
      std::memcpy(frame.data + output.offset, &kind, sizeof(kind));
      std::memcpy(frame.data + output.offset + 8, &begin, sizeof(begin));
      std::memcpy(frame.data + output.offset + 16, &start, sizeof(start));
      std::memcpy(frame.data + output.offset + 24, &end, sizeof(end));
      return OBELISK_RT_OK;
    }
    uint64_t planeSize =
        output.kind == OBELISK_RT_DBREG_LOGIC ? output.size / 2 : output.size;
    void *unknown = output.kind == OBELISK_RT_DBREG_LOGIC
                        ? frame.data + output.offset + planeSize
                        : nullptr;
    return obelisk_rt_v1_container_read_checked(
        readManaged(inputRegister(0)), static_cast<int64_t>(*index),
        frame.data + output.offset, planeSize, unknown,
        unknown ? planeSize : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_WRITE: {
    auto index = scalar(1);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!index)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    Layout input = layoutAt(image, frame.function, inputRegister(2));
    if (input.kind == OBELISK_RT_DBREG_HANDLE) {
      int64_t event = kInvalidHandleStart;
      std::memcpy(&event, frame.data + input.offset + 16, sizeof(event));
      return obelisk_rt_v1_container_write_checked(
          lane, readManaged(inputRegister(0)), static_cast<int64_t>(*index),
          &event, sizeof(event), nullptr, 0);
    }
    uint64_t planeSize =
        input.kind == OBELISK_RT_DBREG_LOGIC ? input.size / 2 : input.size;
    const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                              ? frame.data + input.offset + planeSize
                              : nullptr;
    return obelisk_rt_v1_container_write_checked(
        lane, readManaged(inputRegister(0)), static_cast<int64_t>(*index),
        frame.data + input.offset, planeSize, unknown, unknown ? planeSize : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE: {
    std::array<std::optional<uint64_t>, 9> inputs;
    for (uint32_t index = 0; index != 7; ++index)
      inputs[index] = scalar(index);
    inputs[7] = scalar(8);
    inputs[8] = scalar(9);
    if (std::any_of(inputs.begin(), inputs.end(),
                    [](const auto &value) { return !value; }))
      return OBELISK_RT_INVALID_BYTECODE;
    std::optional<ByteSpan> trace =
        readByteSpan(image, frame, inputRegister(7));
    if (!trace || trace->size % sizeof(obelisk_rt_element_trace_slot_v1) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<obelisk_rt_element_trace_slot_v1> traceSlots(
        trace->size / sizeof(obelisk_rt_element_trace_slot_v1));
    if (!traceSlots.empty())
      std::memcpy(traceSlots.data(), trace->data, trace->size);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *result = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_container_create_typed(
        lane, static_cast<uint32_t>(*inputs[0]), *inputs[1],
        static_cast<uint32_t>(*inputs[2]), static_cast<uint32_t>(*inputs[3]),
        *inputs[4], *inputs[5], *inputs[6], traceSlots.data(),
        traceSlots.size(), *inputs[7], *inputs[8], &result);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_CLONE: {
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *result = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_container_clone(
        lane, readManaged(inputRegister(0)), &result);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_DELETE:
    return obelisk_rt_v1_container_delete(readManaged(inputRegister(0)));
  case OBELISK_RT_INTRINSIC_V1_QUEUE_DELETE: {
    auto index = scalar(1);
    return index
               ? obelisk_rt_v1_queue_delete_index(readManaged(inputRegister(0)),
                                                  static_cast<int64_t>(*index))
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_QUEUE_INSERT: {
    auto index = scalar(1);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!index)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    Layout input = layoutAt(image, frame.function, inputRegister(2));
    if (input.kind == OBELISK_RT_DBREG_HANDLE) {
      int64_t event = kInvalidHandleStart;
      std::memcpy(&event, frame.data + input.offset + 16, sizeof(event));
      return obelisk_rt_v1_queue_insert(lane, readManaged(inputRegister(0)),
                                        static_cast<int64_t>(*index), &event,
                                        nullptr);
    }
    uint64_t planeSize =
        input.kind == OBELISK_RT_DBREG_LOGIC ? input.size / 2 : input.size;
    const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                              ? frame.data + input.offset + planeSize
                              : nullptr;
    return obelisk_rt_v1_queue_insert(lane, readManaged(inputRegister(0)),
                                      static_cast<int64_t>(*index),
                                      frame.data + input.offset, unknown);
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_CREATE: {
    std::array<std::optional<uint64_t>, 8> inputs;
    for (uint32_t index = 0; index != 6; ++index)
      inputs[index] = scalar(index);
    inputs[6] = scalar(7);
    inputs[7] = scalar(8);
    if (std::any_of(inputs.begin(), inputs.end(),
                    [](const auto &value) { return !value; }))
      return OBELISK_RT_INVALID_BYTECODE;
    std::optional<ByteSpan> trace =
        readByteSpan(image, frame, inputRegister(6));
    if (!trace || trace->size % sizeof(obelisk_rt_element_trace_slot_v1) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<obelisk_rt_element_trace_slot_v1> slots(
        trace->size / sizeof(obelisk_rt_element_trace_slot_v1));
    if (!slots.empty())
      std::memcpy(slots.data(), trace->data, trace->size);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *result = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_assoc_create_typed(
        lane, *inputs[0], static_cast<uint32_t>(*inputs[1]),
        static_cast<uint32_t>(*inputs[2]), *inputs[3], *inputs[4], *inputs[5],
        slots.data(), slots.size(), static_cast<uint32_t>(*inputs[6]),
        *inputs[7], &result);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_READ: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    if (!readAssocKey(array, inputRegister(1), key))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    std::memset(frame.data + output.offset, 0, output.size);
    uint32_t present = 0;
    if (output.kind == OBELISK_RT_DBREG_HANDLE) {
      uint64_t event = UINT64_MAX;
      obelisk_rt_status status = obelisk_rt_v1_assoc_read_checked(
          array, &key, &event, sizeof(event), nullptr, 0, &present);
      if (status != OBELISK_RT_OK)
        return status;
      uint32_t kind = OBELISK_RT_DESCRIPTOR_EVENT;
      int64_t start = static_cast<int64_t>(event);
      int64_t begin = start == kInvalidHandleStart ? 0 : start;
      int64_t end = start == kInvalidHandleStart
                        ? 0
                        : (start == INT64_MAX ? start : start + 1);
      std::memcpy(frame.data + output.offset, &kind, sizeof(kind));
      std::memcpy(frame.data + output.offset + 8, &begin, sizeof(begin));
      std::memcpy(frame.data + output.offset + 16, &start, sizeof(start));
      std::memcpy(frame.data + output.offset + 24, &end, sizeof(end));
      return OBELISK_RT_OK;
    }
    uint64_t planeSize =
        output.kind == OBELISK_RT_DBREG_LOGIC ? output.size / 2 : output.size;
    void *unknown = output.kind == OBELISK_RT_DBREG_LOGIC
                        ? frame.data + output.offset + planeSize
                        : nullptr;
    return obelisk_rt_v1_assoc_read_checked(
        array, &key, frame.data + output.offset, planeSize, unknown,
        unknown ? planeSize : 0, &present);
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_WRITE: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    if (!readAssocKey(array, inputRegister(1), key))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(2));
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (input.kind == OBELISK_RT_DBREG_HANDLE) {
      int64_t event = kInvalidHandleStart;
      std::memcpy(&event, frame.data + input.offset + 16, sizeof(event));
      return obelisk_rt_v1_assoc_write_checked(lane, array, &key, &event,
                                               sizeof(event), nullptr, 0);
    }
    uint64_t planeSize =
        input.kind == OBELISK_RT_DBREG_LOGIC ? input.size / 2 : input.size;
    const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                              ? frame.data + input.offset + planeSize
                              : nullptr;
    return obelisk_rt_v1_assoc_write_checked(
        lane, array, &key, frame.data + input.offset, planeSize, unknown,
        unknown ? planeSize : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_EXISTS: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    if (!readAssocKey(array, inputRegister(1), key))
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t exists = 0;
    obelisk_rt_status status = obelisk_rt_v1_assoc_exists(array, &key, &exists);
    return status == OBELISK_RT_OK ? sentinel(0, exists) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_DELETE: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    return readAssocKey(array, inputRegister(1), key)
               ? obelisk_rt_v1_assoc_delete(array, &key)
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_DEFAULT: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (input.kind == OBELISK_RT_DBREG_HANDLE) {
      int64_t event = kInvalidHandleStart;
      std::memcpy(&event, frame.data + input.offset + 16, sizeof(event));
      return obelisk_rt_v1_assoc_set_default_checked(lane, array, &event,
                                                     sizeof(event), nullptr, 0);
    }
    uint64_t planeSize =
        input.kind == OBELISK_RT_DBREG_LOGIC ? input.size / 2 : input.size;
    const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                              ? frame.data + input.offset + planeSize
                              : nullptr;
    return obelisk_rt_v1_assoc_set_default_checked(
        lane, array, frame.data + input.offset, planeSize, unknown,
        unknown ? planeSize : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_TRAVERSE: {
    auto direction = scalar(2);
    auto endpoint = scalar(3);
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    if (!direction || !endpoint || (*endpoint != 0 && *endpoint != 1) ||
        (static_cast<int64_t>(*direction) != -1 &&
         static_cast<int64_t>(*direction) != 1) ||
        !readAssocKey(array, inputRegister(1), key))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    uint32_t succeeded = 0;
    obelisk_rt_status status;
    if (*endpoint)
      status = static_cast<int64_t>(*direction) > 0
                   ? obelisk_rt_v1_assoc_first(lane, array, &key, &succeeded)
                   : obelisk_rt_v1_assoc_last(lane, array, &key, &succeeded);
    else
      status = static_cast<int64_t>(*direction) > 0
                   ? obelisk_rt_v1_assoc_next(lane, array, &key, &succeeded)
                   : obelisk_rt_v1_assoc_prev(lane, array, &key, &succeeded);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeAssocKey(outputRegister(0), key))
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, succeeded);
  }
  case OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_ASSOC: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    obelisk_rt_object_v1 *watchOwner = nullptr;
    uint64_t ownerPayload = 0;
    uint32_t ownerManaged = 0;
    if (!readAssocKey(array, inputRegister(1), key) ||
        !readArgumentRef(inputRegister(2), watchOwner, ownerPayload,
                         ownerManaged))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *path = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_reference_path_assoc_create(
        lane, array, &key, watchOwner, ownerPayload, ownerManaged, &path);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), path)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_BOUNDED: {
    auto bound = scalar(0);
    uint64_t result = 0;
    if (!bound)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_random_bounded(context, *bound, &result);
    return status == OBELISK_RT_OK ? sentinel(0, result) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_DISTRIBUTION: {
    auto distribution = scalar(0), first = scalar(1), second = scalar(2);
    if (!distribution || !first || !second || *distribution > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    int32_t result = 0;
    obelisk_rt_status status = obelisk_rt_v1_random_distribution(
        context, static_cast<obelisk_rt_distribution>(*distribution),
        static_cast<int32_t>(*first), static_cast<int32_t>(*second), &result);
    return status == OBELISK_RT_OK
               ? sentinel(0, static_cast<uint32_t>(result))
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_NEXT: {
    uint64_t result = 0;
    obelisk_rt_status status = obelisk_rt_v1_random_next(context, &result);
    return status == OBELISK_RT_OK ? sentinel(0, result) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SEED: {
    auto seed = scalar(0);
    return seed ? obelisk_rt_v1_random_seed(context, *seed)
                : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE: {
    std::optional<ByteSpan> program = bytes(0);
    auto start = scalar(1), maxAttempts = scalar(2);
    if (!program || !start || !maxAttempts)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint64_t> captures;
    captures.reserve(site.inputCount - 3);
    for (uint32_t index = 3; index != site.inputCount; ++index) {
      auto capture = scalar(index);
      if (!capture)
        return OBELISK_RT_INVALID_BYTECODE;
      captures.push_back(*capture);
    }
    uint64_t assignment = 0;
    uint32_t success = 0;
    obelisk_rt_status status = obelisk_rt_v1_random_solve(
        context, program->data, program->size, *start, *maxAttempts,
        captures.data(), captures.size(), &assignment, &success);
    if (status != OBELISK_RT_OK ||
        !writeScalar(image, frame, outputRegister(0), assignment) ||
        !writeScalar(image, frame, outputRegister(1), success))
      return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_BYTECODE : status;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_CREATE: {
    auto type = scalar(0);
    if (!type || site.inputCount < 2)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint64_t> bins;
    try {
      bins.reserve(site.inputCount - 1);
      for (uint32_t index = 1; index != site.inputCount; ++index) {
        auto count = scalar(index);
        if (!count)
          return OBELISK_RT_INVALID_BYTECODE;
        bins.push_back(*count);
      }
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    } catch (const std::length_error &) {
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    obelisk_rt_covergroup_v1 handle = 0;
    obelisk_rt_status status = obelisk_rt_v1_covergroup_create(
        context, *type, bins.data(), bins.size(), &handle);
    return status == OBELISK_RT_OK ? sentinel(0, handle) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_SET_ENABLED: {
    auto handle = scalar(0);
    auto enabled = scalar(1);
    return handle && enabled && *enabled <= 1
               ? obelisk_rt_v1_covergroup_set_enabled(
                     context, *handle, static_cast<uint32_t>(*enabled))
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_SAMPLE_ENABLED: {
    auto handle = scalar(0);
    uint32_t enabled = 0;
    if (!handle)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_covergroup_sample_enabled(context, *handle, &enabled);
    return status == OBELISK_RT_OK ? sentinel(0, enabled) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_BIN_HIT: {
    auto handle = scalar(0);
    auto coverpoint = scalar(1);
    auto bin = scalar(2);
    return handle && coverpoint && bin && *coverpoint <= UINT32_MAX &&
                   *bin <= UINT32_MAX
               ? obelisk_rt_v1_covergroup_bin_hit(
                     context, *handle, static_cast<uint32_t>(*coverpoint),
                     static_cast<uint32_t>(*bin))
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_SAMPLE: {
    auto handle = scalar(0);
    if (!handle || site.inputCount < 2)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint8_t> hits;
    try {
      hits.reserve(site.inputCount - 1);
      for (uint32_t index = 1; index != site.inputCount; ++index) {
        auto hit = scalar(index);
        if (!hit || *hit > 1)
          return OBELISK_RT_INVALID_BYTECODE;
        hits.push_back(static_cast<uint8_t>(*hit));
      }
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    } catch (const std::length_error &) {
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    return obelisk_rt_v1_covergroup_sample(context, *handle, hits.data(),
                                           hits.size());
  }
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_INSTANCE_QUERY: {
    auto handle = scalar(0);
    if (!handle)
      return OBELISK_RT_INVALID_BYTECODE;
    double percentage = 0.0;
    int32_t covered = 0;
    int32_t total = 0;
    obelisk_rt_status status = obelisk_rt_v1_covergroup_instance_query(
        context, *handle, &percentage, &covered, &total);
    if (status != OBELISK_RT_OK)
      return status;
    status = writeReal(0, percentage);
    if (status != OBELISK_RT_OK)
      return status;
    status = sentinel(1, static_cast<uint32_t>(covered));
    return status == OBELISK_RT_OK ? sentinel(2, static_cast<uint32_t>(total))
                                   : status;
  }
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_TYPE_QUERY: {
    auto type = scalar(0);
    if (!type || site.inputCount < 2)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint64_t> bins;
    try {
      bins.reserve(site.inputCount - 1);
      for (uint32_t index = 1; index != site.inputCount; ++index) {
        auto count = scalar(index);
        if (!count)
          return OBELISK_RT_INVALID_BYTECODE;
        bins.push_back(*count);
      }
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    } catch (const std::length_error &) {
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    double percentage = 0.0;
    int32_t covered = 0;
    int32_t total = 0;
    obelisk_rt_status status = obelisk_rt_v1_covergroup_type_query(
        context, *type, bins.data(), bins.size(), &percentage, &covered,
        &total);
    if (status != OBELISK_RT_OK)
      return status;
    status = writeReal(0, percentage);
    if (status != OBELISK_RT_OK)
      return status;
    status = sentinel(1, static_cast<uint32_t>(covered));
    return status == OBELISK_RT_OK ? sentinel(2, static_cast<uint32_t>(total))
                                   : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_LITERAL: {
    auto literal = bytes(0);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!literal || !lane)
      return literal ? OBELISK_RT_INVALID_LIFECYCLE
                     : OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_create(
        lane, reinterpret_cast<const char *>(literal->data), literal->size,
        &result);
    return status == OBELISK_RT_OK && !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_FROM_PACKED: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    uint64_t planeSize = ((uint64_t{input.width} + 63) / 64) * 8;
    const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                              ? frame.data + input.offset + planeSize
                              : nullptr;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_from_packed(
        lane, frame.data + input.offset, unknown, input.width, &result);
    return status == OBELISK_RT_OK && !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_TO_PACKED: {
    obelisk_rt_string_v1 input = 0;
    if (!readString(inputRegister(0), input))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    uint64_t planeSize = ((uint64_t{output.width} + 63) / 64) * 8;
    void *unknown = output.kind == OBELISK_RT_DBREG_LOGIC
                        ? frame.data + output.offset + planeSize
                        : nullptr;
    return obelisk_rt_v1_string_to_packed(input, frame.data + output.offset,
                                          unknown, output.width);
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_CONCAT: {
    std::vector<obelisk_rt_string_span_v1> spans(site.inputCount);
    for (uint32_t index = 0; index != site.inputCount; ++index)
      if (!readString(inputRegister(index), spans[index].string))
        return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_concat_many(
        lane, spans.data(), spans.size(), &result);
    return status == OBELISK_RT_OK && !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_REPEAT: {
    obelisk_rt_string_v1 input = 0;
    auto count = scalar(1);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !count)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_string_repeat(lane, input, *count, &result);
    return status == OBELISK_RT_OK && !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_LENGTH: {
    obelisk_rt_string_v1 input = 0;
    return readString(inputRegister(0), input)
               ? sentinel(0, obelisk_rt_v1_string_length(input))
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_GETC: {
    obelisk_rt_string_v1 input = 0;
    auto index = scalar(1);
    return readString(inputRegister(0), input) && index
               ? sentinel(0, obelisk_rt_v1_string_getc(
                                 input, static_cast<int64_t>(*index)))
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_PUTC: {
    obelisk_rt_string_v1 input = 0;
    auto index = scalar(1);
    auto character = scalar(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !index || !character)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_string_putc(lane, input, static_cast<int64_t>(*index),
                                  static_cast<uint32_t>(*character), &result);
    return status == OBELISK_RT_OK && !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_SUBSTR: {
    obelisk_rt_string_v1 input = 0;
    auto left = scalar(1);
    auto right = scalar(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !left || !right)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_string_substr(lane, input, static_cast<int64_t>(*left),
                                    static_cast<int64_t>(*right), &result);
    return status == OBELISK_RT_OK && !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_COMPARE: {
    obelisk_rt_string_v1 left = 0;
    obelisk_rt_string_v1 right = 0;
    auto insensitive = scalar(2);
    if (!readString(inputRegister(0), left) ||
        !readString(inputRegister(1), right) || !insensitive ||
        *insensitive > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    int32_t result = *insensitive
                         ? obelisk_rt_v1_string_compare_insensitive(left, right)
                         : obelisk_rt_v1_string_compare(left, right);
    return sentinel(0, static_cast<uint32_t>(result));
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_CASE_CONVERT: {
    obelisk_rt_string_v1 input = 0;
    auto upper = scalar(1);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !upper || *upper > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_case_convert(
        lane, input, static_cast<uint32_t>(*upper), &result);
    return status == OBELISK_RT_OK && !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_SCAN_FIELD: {
    obelisk_rt_string_v1 input = 0;
    auto cursor = scalar(1), specifier = scalar(3);
    auto prefix = bytes(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !cursor || !specifier ||
        !prefix || *cursor > UINT32_MAX || *specifier > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 field = 0;
    uint32_t nextCursor = 0;
    uint32_t ok = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_scan_field(
        lane, input, static_cast<uint32_t>(*cursor),
        reinterpret_cast<const char *>(prefix->data), prefix->size,
        static_cast<uint32_t>(*specifier), &field, &nextCursor, &ok);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(outputRegister(0), field))
      return OBELISK_RT_INVALID_BYTECODE;
    status = sentinel(1, nextCursor);
    return status == OBELISK_RT_OK ? sentinel(2, ok) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_PARSE_INTEGER: {
    obelisk_rt_string_v1 input = 0;
    auto radix = scalar(1);
    if (!readString(inputRegister(0), input) || !radix)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_parse_integer(
        input, static_cast<uint32_t>(*radix), &result);
    return status == OBELISK_RT_OK ? sentinel(0, result) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_PARSE_REAL: {
    obelisk_rt_string_v1 input = 0;
    if (!readString(inputRegister(0), input))
      return OBELISK_RT_INVALID_BYTECODE;
    double result = 0.0;
    obelisk_rt_status status = obelisk_rt_v1_string_parse_real(input, &result);
    return status == OBELISK_RT_OK ? writeReal(0, result) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_INTEGER: {
    auto value = scalar(0);
    auto radix = scalar(1);
    auto signedMode = scalar(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!value || !radix || !signedMode)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_format_integer(
        lane, *value, static_cast<uint32_t>(*radix),
        static_cast<uint32_t>(*signedMode), &result);
    return status == OBELISK_RT_OK && !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_REAL: {
    auto value = realInput(0);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!value)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_string_format_real(lane, *value, &result);
    return status == OBELISK_RT_OK && !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_ALLOC: {
    auto id = scalar(0);
    const obelisk_rt_class_descriptor_v1 *descriptor =
        id ? obelisk_rt_managed_class_lookup(context, *id) : nullptr;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!descriptor || !lane)
      return OBELISK_RT_INVALID_DESIGN;
    obelisk_rt_object_v1 *object = nullptr;
    obelisk_rt_status status =
        obelisk_rt_v1_object_allocate(lane, descriptor, &object);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), object)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_COPY: {
    auto id = scalar(1);
    const obelisk_rt_class_descriptor_v1 *descriptor =
        id ? obelisk_rt_managed_class_lookup(context, *id) : nullptr;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!descriptor || !lane)
      return OBELISK_RT_INVALID_DESIGN;
    obelisk_rt_object_v1 *object = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_object_shallow_copy(
        lane, descriptor, readManaged(inputRegister(0)), &object);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), object)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_IS_INSTANCE: {
    auto id = scalar(1);
    const obelisk_rt_class_descriptor_v1 *descriptor =
        id ? obelisk_rt_managed_class_lookup(context, *id) : nullptr;
    if (!descriptor)
      return OBELISK_RT_INVALID_DESIGN;
    return sentinel(0, obelisk_rt_v1_object_is_instance(
                           readManaged(inputRegister(0)), descriptor));
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_ID:
    return sentinel(0, obelisk_rt_v1_object_id(readManaged(inputRegister(0))));
  case OBELISK_RT_INTRINSIC_V1_CLASS_CAST: {
    auto id = scalar(1);
    const obelisk_rt_class_descriptor_v1 *descriptor =
        id ? obelisk_rt_managed_class_lookup(context, *id) : nullptr;
    if (!descriptor)
      return OBELISK_RT_INVALID_DESIGN;
    obelisk_rt_object_v1 *object = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_object_cast(
        readManaged(inputRegister(0)), descriptor, &object);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), object)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_FIELD_REF: {
    auto offset = scalar(1);
    if (!offset)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    obelisk_rt_object_v1 *object = readManaged(inputRegister(0));
    std::memcpy(frame.data + output.offset, &object, sizeof(object));
    std::memcpy(frame.data + output.offset + 8, &*offset, sizeof(*offset));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_INDEX: {
    auto index = scalar(1);
    if (!index)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout ownerReference = layoutAt(image, frame.function, inputRegister(2));
    if (ownerReference.kind != OBELISK_RT_DBREG_ARGUMENT_REF ||
        ownerReference.size != 24)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t ownerPayload = 0;
    uint32_t ownerManaged = 0;
    obelisk_rt_object_v1 *watchOwner = nullptr;
    std::memcpy(&watchOwner, frame.data + ownerReference.offset,
                sizeof(watchOwner));
    std::memcpy(&ownerPayload, frame.data + ownerReference.offset + 8,
                sizeof(ownerPayload));
    std::memcpy(&ownerManaged, frame.data + ownerReference.offset + 16,
                sizeof(ownerManaged));
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *path = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_reference_path_index_create(
        lane, readManaged(inputRegister(0)), static_cast<int64_t>(*index),
        watchOwner, ownerPayload, ownerManaged, &path);
    if (status != OBELISK_RT_OK)
      return status;
    return writeManaged(outputRegister(0), path) ? OBELISK_RT_OK
                                                 : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_PATH: {
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (output.kind != OBELISK_RT_DBREG_ARGUMENT_REF || output.size != 24)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_object_v1 *path = readManaged(inputRegister(0));
    std::memset(frame.data + output.offset, 0, output.size);
    std::memcpy(frame.data + output.offset, &path, sizeof(path));
    uint64_t managed = 2;
    std::memcpy(frame.data + output.offset + 16, &managed, sizeof(managed));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_REF: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    uint64_t stable = UINT64_MAX;
    if (!encodeCanonicalHandle(frame.data + input.offset, stable))
      return OBELISK_RT_INVALID_HANDLE;
    std::memset(frame.data + output.offset, 0, output.size);
    std::memcpy(frame.data + output.offset + 8, &stable, sizeof(stable));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_MANAGED: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    std::memset(frame.data + output.offset, 0, output.size);
    std::memcpy(frame.data + output.offset, frame.data + input.offset, 16);
    uint64_t managed = 1;
    std::memcpy(frame.data + output.offset + 16, &managed, sizeof(managed));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_LOAD: {
    obelisk_rt_object_v1 *owner = nullptr;
    uint64_t payload = 0;
    uint32_t managed = 0;
    auto planeSize = scalar(1);
    auto bitWidth = scalar(2);
    auto flags = scalar(3);
    if (!readArgumentRef(inputRegister(0), owner, payload, managed) ||
        !planeSize || !bitWidth || !flags || *planeSize == 0 ||
        *bitWidth == 0 || (*flags & ~uint64_t{7}) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    bool fourState = (*flags & 1) != 0;
    uint32_t valueKind = static_cast<uint32_t>(*flags >> 1);
    uint64_t scratchPlaneSize = ((uint64_t{output.width} + 63) / 64) * 8;
    if (*bitWidth != output.width || *planeSize > scratchPlaneSize ||
        output.size != scratchPlaneSize * (fourState ? 2 : 1) ||
        fourState != (output.kind == OBELISK_RT_DBREG_LOGIC) ||
        (valueKind == OBELISK_RT_ARGUMENT_VALUE_CLASS) !=
            (output.kind == OBELISK_RT_DBREG_MANAGED) ||
        (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) !=
            (output.kind == OBELISK_RT_DBREG_STRING) ||
        valueKind > OBELISK_RT_ARGUMENT_VALUE_STRING)
      return OBELISK_RT_INVALID_BYTECODE;
    uint8_t dummy = 0;
    const uint8_t *stateValue =
        context->stateValue.empty()
            ? &dummy
            : reinterpret_cast<const uint8_t *>(context->stateValue.data());
    const uint8_t *stateUnknown =
        context->stateUnknown.empty()
            ? &dummy
            : reinterpret_cast<const uint8_t *>(context->stateUnknown.data());
    return obelisk_rt_v1_argument_ref_load(
        context, stateValue, stateUnknown, image.stateBitCount, owner, payload,
        managed, *bitWidth, *planeSize, fourState, valueKind,
        frame.data + output.offset,
        fourState ? frame.data + output.offset + scratchPlaneSize : nullptr);
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_STORE: {
    obelisk_rt_object_v1 *owner = nullptr;
    uint64_t payload = 0;
    uint32_t managed = 0;
    auto planeSize = scalar(2);
    auto bitWidth = scalar(3);
    auto flags = scalar(4);
    if (!readArgumentRef(inputRegister(0), owner, payload, managed) ||
        !planeSize || !bitWidth || !flags || *planeSize == 0 ||
        *bitWidth == 0 || (*flags & ~uint64_t{7}) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    bool fourState = (*flags & 1) != 0;
    uint32_t valueKind = static_cast<uint32_t>(*flags >> 1);
    uint64_t scratchPlaneSize = ((uint64_t{input.width} + 63) / 64) * 8;
    if (*bitWidth != input.width || *planeSize > scratchPlaneSize ||
        input.size != scratchPlaneSize * (fourState ? 2 : 1) ||
        fourState != (input.kind == OBELISK_RT_DBREG_LOGIC) ||
        (valueKind == OBELISK_RT_ARGUMENT_VALUE_CLASS) !=
            (input.kind == OBELISK_RT_DBREG_MANAGED) ||
        (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) !=
            (input.kind == OBELISK_RT_DBREG_STRING) ||
        valueKind > OBELISK_RT_ARGUMENT_VALUE_STRING)
      return OBELISK_RT_INVALID_BYTECODE;
    uint8_t dummy = 0;
    uint8_t *stateValue =
        context->stateValue.empty()
            ? &dummy
            : reinterpret_cast<uint8_t *>(context->stateValue.data());
    uint8_t *stateUnknown =
        context->stateUnknown.empty()
            ? &dummy
            : reinterpret_cast<uint8_t *>(context->stateUnknown.data());
    return obelisk_rt_v1_argument_ref_store(
        context, stateValue, stateUnknown, image.stateBitCount, owner, payload,
        managed, *bitWidth, *planeSize, fourState, valueKind,
        frame.data + input.offset,
        fourState ? frame.data + input.offset + scratchPlaneSize : nullptr);
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_ROOT_EXTRACT: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    std::optional<uint64_t> bitOffset = scalar(1);
    if (!bitOffset || (*bitOffset & 63) != 0 || *bitOffset > input.width ||
        64 > input.width - *bitOffset)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_object_v1 *object = nullptr;
    std::memcpy(&object, frame.data + input.offset + *bitOffset / 8,
                sizeof(object));
    return writeManaged(outputRegister(0), object)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_LOAD: {
    obelisk_rt_object_v1 *object = nullptr;
    uint64_t offset = 0;
    auto planeSize = scalar(1);
    if (!readManagedRef(inputRegister(0), object, offset) || !planeSize ||
        *planeSize == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (output.kind == OBELISK_RT_DBREG_MANAGED) {
      if (*planeSize != sizeof(obelisk_rt_object_v1 *))
        return OBELISK_RT_INVALID_BYTECODE;
      obelisk_rt_object_v1 *value = nullptr;
      obelisk_rt_status status =
          obelisk_rt_v1_object_field_load(object, offset, &value);
      return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), value)
                 ? OBELISK_RT_INVALID_BYTECODE
                 : status;
    }
    uint64_t scratchPlaneSize = ((uint64_t{output.width} + 63) / 64) * 8;
    bool fourState = output.kind == OBELISK_RT_DBREG_LOGIC;
    if (*planeSize > scratchPlaneSize ||
        output.size != scratchPlaneSize * (fourState ? 2 : 1))
      return OBELISK_RT_INVALID_BYTECODE;
    std::memset(frame.data + output.offset, 0,
                static_cast<size_t>(output.size));
    if (fourState)
      return obelisk_rt_v1_object_read_planes(
          object, offset, frame.data + output.offset,
          frame.data + output.offset + scratchPlaneSize, *planeSize);
    return obelisk_rt_v1_object_read(object, offset, frame.data + output.offset,
                                     *planeSize);
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_STORE: {
    obelisk_rt_object_v1 *object = nullptr;
    uint64_t offset = 0;
    auto planeSize = scalar(2);
    if (!readManagedRef(inputRegister(0), object, offset) || !planeSize ||
        *planeSize == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    if (input.kind == OBELISK_RT_DBREG_MANAGED) {
      if (*planeSize != sizeof(obelisk_rt_object_v1 *))
        return OBELISK_RT_INVALID_BYTECODE;
      return obelisk_rt_v1_object_field_store(object, offset,
                                              readManaged(inputRegister(1)));
    }
    uint64_t scratchPlaneSize = ((uint64_t{input.width} + 63) / 64) * 8;
    bool fourState = input.kind == OBELISK_RT_DBREG_LOGIC;
    if (*planeSize > scratchPlaneSize ||
        input.size != scratchPlaneSize * (fourState ? 2 : 1))
      return OBELISK_RT_INVALID_BYTECODE;
    if (fourState)
      return obelisk_rt_v1_object_write_planes(
          object, offset, frame.data + input.offset,
          frame.data + input.offset + scratchPlaneSize, *planeSize);
    return obelisk_rt_v1_object_write(object, offset, frame.data + input.offset,
                                      *planeSize);
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_NBA: {
    obelisk_rt_object_v1 *object = nullptr;
    uint64_t offset = 0;
    auto planeSize = scalar(2);
    Layout destination = layoutAt(image, frame.function, inputRegister(0));
    bool path = destination.kind == OBELISK_RT_DBREG_MANAGED;
    if (path)
      object = readManaged(inputRegister(0));
    else if (!readManagedRef(inputRegister(0), object, offset))
      return OBELISK_RT_INVALID_BYTECODE;
    if (!planeSize || *planeSize == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    if (path)
      offset = UINT64_MAX;
    uint64_t delay = 0;
    if (site.inputCount == 4) {
      auto encodedDelay = scalar(3);
      if (!encodedDelay)
        return OBELISK_RT_INVALID_BYTECODE;
      delay = *encodedDelay;
    }
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    const void *value = frame.data + input.offset;
    const void *unknown = nullptr;
    if (input.kind == OBELISK_RT_DBREG_MANAGED) {
      if (*planeSize != sizeof(obelisk_rt_object_v1 *))
        return OBELISK_RT_INVALID_BYTECODE;
    } else {
      uint64_t scratchPlaneSize = ((uint64_t{input.width} + 63) / 64) * 8;
      bool fourState = input.kind == OBELISK_RT_DBREG_LOGIC;
      if (*planeSize > scratchPlaneSize ||
          input.size != scratchPlaneSize * (fourState ? 2 : 1))
        return OBELISK_RT_INVALID_BYTECODE;
      if (fourState)
        unknown = frame.data + input.offset + scratchPlaneSize;
    }
    return obelisk_rt_v1_scheduler_managed_nba(context, object, offset, value,
                                               unknown, *planeSize, delay);
  }
  case OBELISK_RT_INTRINSIC_V1_WEAK_CREATE: {
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    std::optional<uint64_t> classID = scalar(1);
    if (!classID)
      return OBELISK_RT_INVALID_BYTECODE;
    const obelisk_rt_class_descriptor_v1 *descriptor =
        obelisk_rt_managed_class_lookup(context, *classID);
    if (!descriptor)
      return OBELISK_RT_INVALID_DESIGN;
    obelisk_rt_object_v1 *weak = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_weak_create(
        lane, descriptor, readManaged(inputRegister(0)), &weak);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), weak)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_WEAK_GET: {
    obelisk_rt_object_v1 *referent = nullptr;
    obelisk_rt_status status =
        obelisk_rt_v1_weak_get(readManaged(inputRegister(0)), &referent);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), referent)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_WEAK_CLEAR:
    return obelisk_rt_v1_weak_clear(readManaged(inputRegister(0)));
  case OBELISK_RT_INTRINSIC_V1_GC_SAFEPOINT: {
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    return lane ? obelisk_rt_v1_gc_safepoint(lane)
                : OBELISK_RT_INVALID_LIFECYCLE;
  }
  case OBELISK_RT_INTRINSIC_V1_SPAWN: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Function callee = functionAt(image, signature.flags);
    uint64_t canonicalSize =
        (callee.flags & OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK) >> 1;
    if (callee.scratchAlignment == 0 ||
        canonicalSize > UINT64_MAX - (callee.scratchAlignment - 1))
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t scratchOffset = (canonicalSize + callee.scratchAlignment - 1) &
                             ~(callee.scratchAlignment - 1);
    if (scratchOffset > UINT64_MAX - callee.scratchSize ||
        scratchOffset + callee.scratchSize > std::numeric_limits<size_t>::max())
      return OBELISK_RT_OUT_OF_MEMORY;
    ScheduledDesignTask task;
    task.parent = context->activeLogicalProcessToken;
    obelisk_rt_random_split_unlocked(context, task.random);
    task.function = signature.flags;
    task.scheduleRank = static_cast<uint32_t>(callee.initialScheduleRank);
    task.scratchOffset = scratchOffset;
    task.scratchSize = callee.scratchSize;
    task.frame = context->designTaskFrames.acquire(
        static_cast<size_t>(scratchOffset + callee.scratchSize));
    uint32_t copied = 0;
    std::unordered_map<uint32_t, uint64_t> retainedAutomaticStates;
    for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
      CaptureRecord capture = captureAt(image, index);
      if (capture.function != signature.flags)
        continue;
      ++copied;
      if (capture.valueOffset == UINT64_MAX)
        continue;
      uint32_t sourceRegister = inputRegister(capture.argument);
      Layout source = layoutAt(image, frame.function, sourceRegister);
      if (source.kind == OBELISK_RT_DBREG_HANDLE) {
        uint64_t stable = UINT64_MAX;
        if (!encodeCanonicalHandle(frame.data + source.offset, stable))
          return OBELISK_RT_INVALID_HANDLE;
        std::memcpy(task.frame.data() + capture.valueOffset, &stable, 8);
        uint32_t automaticID = 0;
        int64_t automaticOffset = 0;
        if (decodeAutomaticHandle(stable, automaticID, automaticOffset) &&
            ++retainedAutomaticStates[automaticID] == 0)
          return OBELISK_RT_OUT_OF_RESOURCES;
        continue;
      }
      std::memcpy(task.frame.data() + capture.valueOffset,
                  frame.data + source.offset, capture.planeSize);
      if (capture.unknownOffset != UINT64_MAX)
        std::memcpy(task.frame.data() + capture.unknownOffset,
                    frame.data + source.offset + capture.planeSize,
                    capture.planeSize);
    }
    if (copied != callee.argumentCount)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t id = 0;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->nextDesignTaskID == 0 ||
          context->nextDesignTaskID > uint64_t{INT64_MAX} ||
          context->nextProcessInsertionSequence == 0 ||
          context->nextProcessInsertionSequence == UINT64_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      for (const auto &[automaticID, count] : retainedAutomaticStates) {
        auto found = context->nativeAutomaticStates.find(automaticID);
        if (found == context->nativeAutomaticStates.end())
          return OBELISK_RT_INVALID_HANDLE;
        if (count > UINT64_MAX - found->second.referenceCount)
          return OBELISK_RT_OUT_OF_RESOURCES;
      }
      id = context->nextDesignTaskID++;
      task.id = id;
      task.phase =
          (callee.flags & OBELISK_RT_DESIGN_FUNCTION_FINAL) != 0 ? 1 : 0;
      task.homeRegion = functionHomeRegion(callee);
      task.queuedRegion = task.homeRegion;
      if (task.phase == 0 && context->activeDesignTaskID != 0)
        task.phase = context->activeDesignTaskPhase;
      if (task.phase == 0 && context->activeNativeProcess)
        for (const ScheduledProcess &process : context->scheduledProcesses)
          if (process.instance == context->activeNativeProcess) {
            task.phase = process.phase;
            break;
          }
      task.controls = context->activeControls;
      task.insertionSequence = context->nextProcessInsertionSequence++;
      task.observedEpoch = context->schedulerEpoch;
      for (const auto &[automaticID, count] : retainedAutomaticStates)
        context->nativeAutomaticStates.find(automaticID)
            ->second.referenceCount += count;
      try {
        context->scheduledDesignTasks.push_back(std::move(task));
        uint64_t scheduledID = context->scheduledDesignTasks.back().id;
        try {
          context->scheduledDesignTaskIndices[scheduledID] =
              context->scheduledDesignTasks.size() - 1;
          context->designPollCandidates.insert(scheduledID);
        } catch (...) {
          context->scheduledDesignTaskIndices.erase(scheduledID);
          context->designPollCandidates.erase(scheduledID);
          context->scheduledDesignTasks.pop_back();
          throw;
        }
        obelisk_rt_retain_controls_unlocked(
            context, context->scheduledDesignTasks.back().controls);
      } catch (...) {
        for (const auto &[automaticID, count] : retainedAutomaticStates)
          context->nativeAutomaticStates.find(automaticID)
              ->second.referenceCount -= count;
        throw;
      }
    }
    uint32_t destinationRegister = outputRegister(0);
    Layout destination = layoutAt(image, frame.function, destinationRegister);
    uint8_t *address = frame.data + destination.offset;
    std::memset(address, 0, destination.size);
    uint32_t kind = OBELISK_RT_DESCRIPTOR_PROCESS;
    int64_t begin = static_cast<int64_t>(id);
    int64_t end = id == uint64_t{INT64_MAX} ? begin : begin + 1;
    std::memcpy(address, &kind, 4);
    std::memcpy(address + 8, &begin, 8);
    std::memcpy(address + 16, &begin, 8);
    std::memcpy(address + 24, &end, 8);
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_NBA:
  case OBELISK_RT_INTRINSIC_V1_STATIC_NBA: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout destination = layoutAt(image, frame.function, inputRegister(1));
    uint32_t kind = 0;
    int64_t begin = 0, start = kInvalidHandleStart, end = 0;
    const uint8_t *address = frame.data + destination.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&start, address + 16, 8);
    std::memcpy(&end, address + 24, 8);
    bool automatic = (kind & kAutomaticHandleKind) != 0;
    uint32_t descriptorKind = kind & ~kAutomaticHandleKind;
    uint64_t objectBase = 0;
    uint32_t objectID = 0;
    std::memcpy(&objectBase, address + 8, 8);
    bool boundedStatic =
        !automatic && decodeStaticHandle(objectBase, objectID, begin);
    if (automatic) {
      if (!decodeAutomaticHandle(objectBase, objectID, begin))
        return OBELISK_RT_INVALID_HANDLE;
    } else if (!boundedStatic) {
      begin = static_cast<int64_t>(objectBase);
    }
    if (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE || begin > end)
      return OBELISK_RT_INVALID_HANDLE;
    // An invalid dynamic selection is an ignored assignment, matching direct
    // state stores and the native scheduler ABI.
    if (start == kInvalidHandleStart)
      return OBELISK_RT_OK;
    Layout valueLayout = layoutAt(image, frame.function, inputRegister(0));
    bool stringValue = valueLayout.kind == OBELISK_RT_DBREG_STRING;
    bool managedValue = valueLayout.kind == OBELISK_RT_DBREG_MANAGED;
    obelisk_rt_string_v1 rootedString = 0;
    if (stringValue && !readString(inputRegister(0), rootedString))
      return OBELISK_RT_INVALID_HANDLE;
    obelisk_rt_object_v1 *rootedManaged =
        managedValue ? readManaged(inputRegister(0)) : nullptr;
    Logic value = readLogic(frame.data, valueLayout);
    uint64_t delay = 0;
    bool staticSite = signature.id == OBELISK_RT_INTRINSIC_V1_STATIC_NBA;
    uint64_t staticSiteID = UINT64_MAX;
    uint32_t delayInputCount = staticSite ? 2 : site.inputCount;
    if (delayInputCount == 3) {
      auto encodedDelay = scalar(2);
      if (!encodedDelay)
        return OBELISK_RT_INVALID_BYTECODE;
      delay = *encodedDelay;
    }
    if (staticSite) {
      auto encodedSite = scalar(2);
      if (!encodedSite || *encodedSite == UINT64_MAX)
        return OBELISK_RT_INVALID_BYTECODE;
      staticSiteID = *encodedSite;
    }
    if (managedValue || automatic || boundedStatic) {
      int64_t first = start < begin ? begin - start : 0;
      int64_t last = static_cast<int64_t>(value.width);
      if (start > end || end - start < last)
        last = end - start;
      if (first >= last)
        return OBELISK_RT_OK;
      if ((stringValue || managedValue) && (first != 0 || last != 64))
        return OBELISK_RT_INVALID_BYTECODE;
      int64_t selectedStart = start + first;
      uint64_t stable =
          automatic       ? encodeAutomaticHandle(objectID, selectedStart)
          : boundedStatic ? encodeStaticHandle(objectID, selectedStart)
                          : static_cast<uint64_t>(selectedStart);
      if (stable == UINT64_MAX)
        return OBELISK_RT_INVALID_HANDLE;
      uint64_t selectedWidth = static_cast<uint64_t>(last - first);
      if (staticSiteID != UINT64_MAX && !stringValue && !managedValue &&
          boundedStatic && selectedWidth <= 64 && context->nativeSchedulePlan &&
          !context->nativeScheduleDeoptimized) {
        uint64_t packedValue = extractScalarBits(
            value.value, static_cast<uint64_t>(first), selectedWidth);
        uint64_t packedUnknown =
            value.fourState
                ? extractScalarBits(value.unknown, static_cast<uint64_t>(first),
                                    selectedWidth)
                : 0;
        uint8_t *valuePlane =
            reinterpret_cast<uint8_t *>(context->stateValue.data());
        uint8_t *unknownPlane =
            value.fourState
                ? reinterpret_cast<uint8_t *>(context->stateUnknown.data())
                : nullptr;
        return obelisk_rt_v1_scheduler_static_nba(
            context, staticSiteID, valuePlane, unknownPlane,
            context->execution->state_bit_count, stable, selectedWidth,
            reinterpret_cast<const uint8_t *>(&packedValue),
            value.fourState ? reinterpret_cast<const uint8_t *>(&packedUnknown)
                            : nullptr);
      }
      ScheduledNBA update;
      update.valuePlane = nullptr;
      update.unknownPlane = nullptr;
      update.planeBitCount = automatic ? static_cast<uint64_t>(end)
                                       : context->execution->state_bit_count;
      update.bitOffset = stable;
      update.bitWidth = selectedWidth;
      update.stringValue = stringValue;
      update.managedValue = managedValue;
      update.rootedString = rootedString;
      update.rootedManaged = rootedManaged;
      uint64_t bytes = (update.bitWidth + 7) / 8;
      update.value.assign(static_cast<size_t>(bytes), 0);
      if (value.fourState)
        update.unknown.assign(static_cast<size_t>(bytes), 0);
      for (uint64_t bitIndex = 0; bitIndex != update.bitWidth; ++bitIndex) {
        uint64_t source = static_cast<uint64_t>(first) + bitIndex;
        uint8_t mask = static_cast<uint8_t>(1u << (bitIndex % 8));
        if (bit(value.value, source))
          update.value[bitIndex / 8] |= mask;
        if (value.fourState && bit(value.unknown, source))
          update.unknown[bitIndex / 8] |= mask;
      }
      if (staticSiteID != UINT64_MAX && !stringValue && !managedValue &&
          boundedStatic && context->nativeSchedulePlan &&
          !context->nativeScheduleDeoptimized) {
        uint8_t *valuePlane =
            reinterpret_cast<uint8_t *>(context->stateValue.data());
        uint8_t *unknownPlane =
            value.fourState
                ? reinterpret_cast<uint8_t *>(context->stateUnknown.data())
                : nullptr;
        return obelisk_rt_v1_scheduler_static_nba(
            context, staticSiteID, valuePlane, unknownPlane,
            context->execution->state_bit_count, stable, update.bitWidth,
            update.value.data(),
            value.fourState ? update.unknown.data() : nullptr);
      }
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->nextSchedulerSequence == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
      update.execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (update.execRegion == UINT32_MAX)
        return OBELISK_RT_INVALID_LIFECYCLE;
      if (automatic) {
        auto found = context->nativeAutomaticStates.find(objectID);
        if (found == context->nativeAutomaticStates.end())
          return OBELISK_RT_INVALID_HANDLE;
        if (found->second.referenceCount == UINT64_MAX)
          return OBELISK_RT_OUT_OF_RESOURCES;
        update.retainedAutomaticID = objectID;
      }
      update.sequence = context->nextSchedulerSequence++;
      update.dueTime = delay > UINT64_MAX - context->schedulerTime
                           ? UINT64_MAX
                           : context->schedulerTime + delay;
      context->scheduledNBAs.push_back(std::move(update));
      if (automatic)
        ++context->nativeAutomaticStates.find(objectID)->second.referenceCount;
      return OBELISK_RT_OK;
    }
    if (stringValue && (value.width != 64 || start < begin || start < 0 ||
                        end < start || end - start < 64))
      return OBELISK_RT_INVALID_BYTECODE;
    ScheduledDesignNBA update;
    update.handleKind = kind;
    update.begin = begin;
    update.start = start;
    update.end = end;
    update.bitWidth = value.width;
    update.stringValue = stringValue;
    update.rootedString = rootedString;
    update.value = std::move(value.value).takeVector();
    update.unknown = std::move(value.unknown).takeVector();
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->nextSchedulerSequence == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
      update.execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (update.execRegion == UINT32_MAX)
        return OBELISK_RT_INVALID_LIFECYCLE;
      update.sequence = context->nextSchedulerSequence++;
      update.dueTime = delay > UINT64_MAX - context->schedulerTime
                           ? UINT64_MAX
                           : context->schedulerTime + delay;
      context->scheduledDesignNBAs.push_back(std::move(update));
    }
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout event = layoutAt(image, frame.function, inputRegister(0));
    uint32_t kind = 0;
    int64_t start = -1;
    const uint8_t *address = frame.data + event.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&start, address + 16, 8);
    if (kind != OBELISK_RT_DESCRIPTOR_EVENT || start < 0)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t stableID = static_cast<uint64_t>(start);
    uint64_t delay = 0;
    if (site.inputCount == 2) {
      std::optional<uint64_t> encodedDelay = scalar(1);
      if (!encodedDelay)
        return OBELISK_RT_INVALID_BYTECODE;
      delay = *encodedDelay;
    }
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint32_t retainedAutomaticID = 0;
    int64_t automaticOffset = 0;
    if (decodeAutomaticHandle(stableID, retainedAutomaticID, automaticOffset)) {
      auto found = context->nativeAutomaticStates.find(retainedAutomaticID);
      if (found == context->nativeAutomaticStates.end())
        return OBELISK_RT_INVALID_HANDLE;
      if (signature.flags != 0 && found->second.referenceCount == UINT64_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
    }
    if (signature.flags != 0) {
      if (context->nextSchedulerSequence == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
      uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                             ? UINT64_MAX
                             : context->schedulerTime + delay;
      uint32_t execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (execRegion == UINT32_MAX)
        return OBELISK_RT_INVALID_LIFECYCLE;
      context->scheduledDesignEvents.push_back({context->nextSchedulerSequence,
                                                dueTime, execRegion, stableID,
                                                retainedAutomaticID});
      if (retainedAutomaticID != 0)
        ++context->nativeAutomaticStates.find(retainedAutomaticID)
              ->second.referenceCount;
      ++context->nextSchedulerSequence;
      return OBELISK_RT_OK;
    }
    EventState &eventState = context->events[stableID];
    if (++eventState.generation == 0)
      eventState.generation = 1;
    eventState.lastTriggeredTime = context->schedulerTime;
    if (!obelisk_rt_notify_observer_event_unlocked(context, stableID))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_DESIGN
                 : context->schedulerStatus;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGERED: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout event = layoutAt(image, frame.function, inputRegister(0));
    uint32_t kind = 0;
    int64_t start = -1;
    const uint8_t *address = frame.data + event.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&start, address + 16, 8);
    if (kind != OBELISK_RT_DESCRIPTOR_EVENT || start < 0)
      return OBELISK_RT_INVALID_HANDLE;
    uint32_t triggered = obelisk_rt_v1_scheduler_event_triggered(
        context, static_cast<uint64_t>(start));
    return sentinel(0, triggered);
  }
  case OBELISK_RT_INTRINSIC_V1_STATE_ALLOC: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout initialLayout = layoutAt(image, frame.function, inputRegister(0));
    uint64_t stable = UINT64_MAX;
    uint64_t initialWidth = initialLayout.width;
    obelisk_rt_status status = OBELISK_RT_OK;
    if (initialLayout.kind == OBELISK_RT_DBREG_MANAGED) {
      obelisk_rt_object_v1 *initial = readManaged(inputRegister(0));
      status = obelisk_rt_native_state_alloc_managed(context, initial, &stable);
    } else {
      Logic initial = readLogic(frame.data, initialLayout);
      initialWidth = initial.width;
      const uint8_t *value =
          reinterpret_cast<const uint8_t *>(initial.value.data());
      const uint8_t *unknown =
          initial.fourState
              ? reinterpret_cast<const uint8_t *>(initial.unknown.data())
              : nullptr;
      if (site.inputCount > 1) {
        std::vector<uint64_t> rootOffsets;
        rootOffsets.reserve(site.inputCount - 1);
        for (uint32_t index = 1; index != site.inputCount; ++index) {
          std::optional<uint64_t> rootOffset = scalar(index);
          if (!rootOffset)
            return OBELISK_RT_INVALID_BYTECODE;
          rootOffsets.push_back(*rootOffset);
        }
        status = obelisk_rt_native_state_alloc_with_root_offsets(
            context, initial.width, value, unknown, std::move(rootOffsets),
            &stable);
      } else
        status = obelisk_rt_v1_native_state_alloc(context, initial.width, value,
                                                  unknown, &stable);
    }
    if (status != OBELISK_RT_OK)
      return status;
    uint32_t id = 0;
    int64_t offset = 0;
    if (!decodeAutomaticHandle(stable, id, offset) || offset != 0)
      return OBELISK_RT_INVALID_HANDLE;
    Layout destination = layoutAt(image, frame.function, outputRegister(0));
    uint8_t *address = frame.data + destination.offset;
    std::memset(address, 0, destination.size);
    uint32_t kind = kAutomaticHandleKind | OBELISK_RT_DESCRIPTOR_STORAGE;
    uint64_t base = stable;
    int64_t begin = 0;
    int64_t start = 0;
    int64_t end = static_cast<int64_t>(initialWidth);
    std::memcpy(address, &kind, sizeof(kind));
    std::memcpy(address + 8, &base, sizeof(base));
    std::memcpy(address + 16, &start, sizeof(start));
    std::memcpy(address + 24, &end, sizeof(end));
    (void)begin;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_DISABLE_CHILDREN:
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    return obelisk_rt_v1_scheduler_disable_children(context);
  case OBELISK_RT_INTRINSIC_V1_CONTROL_ENTER: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t activation = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_control_enter(context, signature.flags, &activation);
    return status == OBELISK_RT_OK ? sentinel(0, activation) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CONTROL_LEAVE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> activation = scalar(0);
    if (!activation)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_control_leave(context, *activation);
  }
  case OBELISK_RT_INTRINSIC_V1_CONTROL_DISABLE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t activation = 0;
    if (site.inputCount != 0) {
      std::optional<uint64_t> value = scalar(0);
      if (!value)
        return OBELISK_RT_INVALID_BYTECODE;
      activation = *value;
    }
    return obelisk_rt_v1_control_disable(context,
                                         signature.flags & ~(UINT32_C(1) << 31),
                                         activation, signature.flags >> 31);
  }
  case OBELISK_RT_INTRINSIC_V1_STATIC_ONCE:
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    return sentinel(0, obelisk_rt_v1_static_once(context, signature.flags));
  case OBELISK_RT_INTRINSIC_V1_DEFERRED_ONCE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> siteID = scalar(0);
    if (!siteID || *siteID == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(0, obelisk_rt_v1_deferred_once(context, *siteID));
  }
  case OBELISK_RT_INTRINSIC_V1_MONITOR_REGISTER: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout process = layoutAt(image, frame.function, inputRegister(0));
    if (process.kind != OBELISK_RT_DBREG_HANDLE || process.size < 32)
      return OBELISK_RT_INVALID_BYTECODE;
    const uint8_t *address = frame.data + process.offset;
    uint32_t kind = 0;
    int64_t begin = 0, start = kInvalidHandleStart, end = 0;
    std::memcpy(&kind, address, 4);
    std::memcpy(&begin, address + 8, 8);
    std::memcpy(&start, address + 16, 8);
    std::memcpy(&end, address + 24, 8);
    if (kind != OBELISK_RT_DESCRIPTOR_PROCESS || begin <= 0 || begin != start ||
        end != begin + 1)
      return OBELISK_RT_INVALID_HANDLE;
    return obelisk_rt_v1_monitor_register(context, static_cast<uint64_t>(begin),
                                          1);
  }
  case OBELISK_RT_INTRINSIC_V1_MONITOR_CONTROL:
    return obelisk_rt_v1_monitor_control(context, signature.flags);
  case OBELISK_RT_INTRINSIC_V1_MONITOR_CURRENT:
    return sentinel(0, obelisk_rt_v1_monitor_current(context));
  case OBELISK_RT_INTRINSIC_V1_IMPORT:
  case OBELISK_RT_INTRINSIC_V1_DPI_IMPORT: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint32_t firstInput = 0;
    obelisk_rt_import_site_v1 importSite{
        OBELISK_RT_VERSION,
        0,
        signature.flags,
        0,
        UINT64_MAX,
        nullptr,
        0,
        0,
        0,
        0,
    };
    uint32_t dataOutputCount = site.outputCount;
    std::vector<uint8_t> dpiInputFlags;
    std::vector<uint8_t> dpiOutputFlags;
    if (signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT) {
      auto metadata = bytes(0);
      if (!metadata || metadata->size < 56 || site.outputCount == 0)
        return OBELISK_RT_INVALID_BYTECODE;
      importSite.version = read32(metadata->data);
      importSite.flags = read32(metadata->data + 4);
      importSite.import_id = read32(metadata->data + 8);
      importSite.reserved = read32(metadata->data + 12);
      importSite.scope_id = read64(metadata->data + 16);
      importSite.source_line = read32(metadata->data + 24);
      importSite.source_column = read32(metadata->data + 28);
      importSite.source_file_size = read64(metadata->data + 32);
      importSite.abi_signature = read64(metadata->data + 40);
      uint32_t logicalInputs = read32(metadata->data + 48);
      uint32_t logicalOutputs = read32(metadata->data + 52);
      uint64_t entryCount = uint64_t{logicalInputs} + uint64_t{logicalOutputs};
      if (entryCount > (UINT64_MAX - 56) / 16)
        return OBELISK_RT_INVALID_BYTECODE;
      uint64_t sourceOffset = 56 + entryCount * 16;
      if (sourceOffset > metadata->size ||
          importSite.source_file_size != metadata->size - sourceOffset ||
          uint64_t{site.inputCount} != uint64_t{logicalInputs} + 1 ||
          uint64_t{site.outputCount} != uint64_t{logicalOutputs} + 1)
        return OBELISK_RT_INVALID_BYTECODE;
      importSite.source_file =
          importSite.source_file_size
              ? reinterpret_cast<const char *>(metadata->data + sourceOffset)
              : nullptr;

      struct ABIEntry {
        uint32_t kind;
        uint32_t direction;
        uint32_t width;
        uint32_t flags;
      };
      auto entry = [&](uint64_t index) {
        const uint8_t *data = metadata->data + 56 + index * 16;
        return ABIEntry{read32(data), read32(data + 4), read32(data + 8),
                        read32(data + 12)};
      };
      auto validEntry = [](ABIEntry abi) {
        if (abi.kind > 7 || abi.direction > 3 || abi.width == 0 ||
            (abi.flags & ~uint32_t{3}) != 0)
          return false;
        bool fourState = (abi.flags & 1) != 0;
        switch (abi.kind) {
        case 0:
          return abi.width == 1 && !fourState;
        case 1:
          return abi.width == 1 && fourState;
        case 2:
          return abi.width == 8 && !fourState;
        case 3:
          return abi.width == 16 && !fourState;
        case 4:
          return abi.width == 32 && !fourState;
        case 5:
          return abi.width == 64 && !fourState;
        case 6:
          return !fourState;
        case 7:
          return fourState;
        }
        return false;
      };
      auto sameValue = [](ABIEntry left, ABIEntry right) {
        return left.kind == right.kind && left.width == right.width &&
               left.flags == right.flags;
      };
      auto matchesLayout = [](ABIEntry abi, Layout layout) {
        uint8_t expectedKind = (abi.flags & 1) != 0 ? OBELISK_RT_DBREG_LOGIC
                                                    : OBELISK_RT_DBREG_BITS;
        return layout.kind == expectedKind && layout.width == abi.width;
      };
      uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
      auto appendHash = [&](uint64_t value, unsigned bytes) {
        hash = obelisk_stable_hash_append_uint_le(hash, value, bytes);
      };
      appendHash(logicalInputs, 8);
      appendHash(logicalOutputs, 8);
      for (uint64_t index = 0; index != entryCount; ++index) {
        ABIEntry abi = entry(index);
        if (!validEntry(abi))
          return OBELISK_RT_INVALID_BYTECODE;
        appendHash(abi.kind, 4);
        appendHash(abi.direction, 4);
        appendHash(abi.width, 4);
        appendHash((abi.flags & 1) != 0, 1);
        appendHash((abi.flags & 2) != 0, 1);
      }
      if (hash == 0)
        hash = 1;
      if (hash != importSite.abi_signature)
        return OBELISK_RT_INVALID_BYTECODE;
      for (uint32_t index = 0; index != logicalInputs; ++index) {
        ABIEntry abi = entry(index);
        if (abi.direction == 3 ||
            !matchesLayout(
                abi, layoutAt(image, frame.function, inputRegister(index + 1))))
          return OBELISK_RT_INVALID_BYTECODE;
        dpiInputFlags.push_back((abi.flags & 2) != 0 ? OBELISK_RT_DBREG_SIGNED
                                                     : 0);
      }
      for (uint32_t index = 0; index != logicalOutputs; ++index) {
        ABIEntry abi = entry(uint64_t{logicalInputs} + index);
        if (!matchesLayout(
                abi, layoutAt(image, frame.function, outputRegister(index))))
          return OBELISK_RT_INVALID_BYTECODE;
        dpiOutputFlags.push_back((abi.flags & 2) != 0 ? OBELISK_RT_DBREG_SIGNED
                                                      : 0);
      }
      Layout statusLayout =
          layoutAt(image, frame.function, outputRegister(logicalOutputs));
      if (statusLayout.kind != OBELISK_RT_DBREG_STATUS)
        return OBELISK_RT_INVALID_BYTECODE;

      uint64_t outputCursor = logicalInputs;
      bool task = (importSite.flags & OBELISK_RT_IMPORT_TASK) != 0;
      if (!task && outputCursor < entryCount &&
          entry(outputCursor).direction == 3)
        ++outputCursor;
      for (uint32_t index = 0; index != logicalInputs; ++index) {
        ABIEntry input = entry(index);
        if (input.direction == 0)
          continue;
        if (outputCursor >= entryCount)
          return OBELISK_RT_INVALID_BYTECODE;
        ABIEntry output = entry(outputCursor++);
        if (output.direction != 1 || !sameValue(input, output))
          return OBELISK_RT_INVALID_BYTECODE;
      }
      if (outputCursor != entryCount)
        return OBELISK_RT_INVALID_BYTECODE;

      firstInput = 1;
      dataOutputCount = logicalOutputs;
    }
    uint32_t inputCount = site.inputCount - firstInput;
    std::vector<obelisk_rt_import_input_v1> inputs;
    std::vector<obelisk_rt_import_output_v1> outputs;
    inputs.reserve(inputCount);
    outputs.reserve(dataOutputCount);
    auto describe = [&](Layout layout, uint8_t *address) {
      uint64_t limbs = layout.kind == OBELISK_RT_DBREG_STATUS ? 1
                       : layout.kind == OBELISK_RT_DBREG_HANDLE
                           ? 4
                           : limbCount(layout.width);
      uint32_t width =
          layout.kind == OBELISK_RT_DBREG_STATUS ? 32 : layout.width;
      return std::tuple<uint32_t, uint64_t, uint64_t *, uint64_t *>{
          width, limbs, reinterpret_cast<uint64_t *>(address),
          layout.kind == OBELISK_RT_DBREG_LOGIC
              ? reinterpret_cast<uint64_t *>(address + limbs * 8)
              : nullptr};
    };
    for (uint32_t index = 0; index != inputCount; ++index) {
      Layout layout =
          layoutAt(image, frame.function, inputRegister(index + firstInput));
      auto [width, limbs, value, unknown] =
          describe(layout, frame.data + layout.offset);
      uint8_t flags = signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT
                          ? dpiInputFlags[index]
                          : layout.flags;
      inputs.push_back({layout.kind, flags, 0, width, value, unknown, limbs});
    }
    for (uint32_t index = 0; index != dataOutputCount; ++index) {
      Layout layout = layoutAt(image, frame.function, outputRegister(index));
      uint8_t *address = frame.data + layout.offset;
      auto [width, limbs, value, unknown] = describe(layout, address);
      uint8_t flags = signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT
                          ? dpiOutputFlags[index]
                          : layout.flags;
      outputs.push_back({layout.kind, flags, 0, width, value, unknown, limbs});
    }
    obelisk_rt_status importStatus =
        obelisk_rt_v1_import_call(context, &importSite, inputs.data(),
                                  inputCount, outputs.data(), dataOutputCount);
    if (signature.id != OBELISK_RT_INTRINSIC_V1_DPI_IMPORT)
      return importStatus;
    Layout statusLayout =
        layoutAt(image, frame.function, outputRegister(dataOutputCount));
    uint8_t *statusAddress = frame.data + statusLayout.offset;
    std::memset(statusAddress, 0, statusLayout.size);
    uint64_t statusBits = static_cast<uint32_t>(importStatus);
    std::memcpy(statusAddress, &statusBits, sizeof(statusBits));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_DISPLAY: {
    auto metadata = bytes(0);
    auto descriptor = scalar(1);
    if (!metadata || !descriptor || descriptor.value() > UINT32_MAX ||
        metadata->size < 44 || read32(metadata->data) != 1)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t newline = read32(metadata->data + 4);
    uint32_t radix = read32(metadata->data + 8);
    uint32_t itemCount = read32(metadata->data + 12);
    uint64_t scopeSize = read64(metadata->data + 16);
    uint64_t librarySize = read64(metadata->data + 24);
    uint64_t multiplier = read64(metadata->data + 32);
    int32_t precision = static_cast<int32_t>(read32(metadata->data + 40));
    uint64_t flagsSize = uint64_t{itemCount} * 4;
    if (newline > 1 ||
        (radix != OBELISK_RT_RADIX_BINARY && radix != OBELISK_RT_RADIX_OCTAL &&
         radix != OBELISK_RT_RADIX_DECIMAL && radix != OBELISK_RT_RADIX_HEX) ||
        multiplier == 0 || flagsSize > metadata->size - 44 ||
        scopeSize > metadata->size - 44 - flagsSize ||
        librarySize > metadata->size - 44 - flagsSize - scopeSize)
      return OBELISK_RT_INVALID_BYTECODE;
    const uint8_t *flags = metadata->data + 44;
    const char *scope = reinterpret_cast<const char *>(flags + flagsSize);
    const char *library = scope + scopeSize;
    uint32_t physical = 2;
    std::vector<Logic> values;
    values.reserve(site.inputCount - 2);
    std::vector<double> realValues;
    realValues.reserve(itemCount);
    std::vector<obelisk_rt_arg_v1> arguments;
    arguments.reserve(itemCount);
    for (uint32_t index = 0; index != itemCount; ++index) {
      uint32_t itemFlags = read32(flags + uint64_t{index} * 4);
      if ((itemFlags & ~uint32_t{31}) != 0 ||
          ((itemFlags & 4) != 0 && (itemFlags & 3) != 0))
        return OBELISK_RT_INVALID_BYTECODE;
      if ((itemFlags & 2) != 0) {
        arguments.push_back({OBELISK_RT_ARG_EMPTY, 0, 0, nullptr, nullptr});
        continue;
      }
      if (physical >= site.inputCount)
        return OBELISK_RT_INVALID_BYTECODE;
      uint32_t reg = inputRegister(physical++);
      Layout layout = layoutAt(image, frame.function, reg);
      if (layout.kind == OBELISK_RT_DBREG_BYTES) {
        auto value = readByteSpan(image, frame, reg);
        if (!value || (itemFlags & 5) != 0)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back({OBELISK_RT_ARG_STRING,
                             OBELISK_RT_ARG_FORMAT_STRING, value->size,
                             value->data, nullptr});
      } else if ((itemFlags & 8) != 0) {
        if (layout.kind != OBELISK_RT_DBREG_STRING || layout.size != 8 ||
            (itemFlags & 7) != 0)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back({OBELISK_RT_ARG_MANAGED_STRING,
                             OBELISK_RT_ARG_FORMAT_STRING, 0,
                             frame.data + layout.offset, nullptr});
      } else if ((itemFlags & 16) != 0) {
        if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8 ||
            (itemFlags & 15) != 0)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back({OBELISK_RT_ARG_MANAGED_CONTAINER, 0, 0,
                             frame.data + layout.offset, nullptr});
      } else if ((itemFlags & 4) != 0) {
        if (layout.kind != OBELISK_RT_DBREG_REAL32 &&
            layout.kind != OBELISK_RT_DBREG_REAL64)
          return OBELISK_RT_INVALID_BYTECODE;
        double real = 0.0;
        if (layout.kind == OBELISK_RT_DBREG_REAL32) {
          float value = 0.0f;
          std::memcpy(&value, frame.data + layout.offset, sizeof(value));
          real = value;
        } else {
          std::memcpy(&real, frame.data + layout.offset, sizeof(real));
        }
        realValues.push_back(real);
        arguments.push_back(
            {OBELISK_RT_ARG_REAL, 0, 0, &realValues.back(), nullptr});
      } else {
        values.push_back(readLogic(frame.data, layout));
        Logic &value = values.back();
        arguments.push_back({OBELISK_RT_ARG_LOGIC,
                             static_cast<obelisk_rt_arg_flags>(
                                 (itemFlags & 1) ? OBELISK_RT_ARG_SIGNED : 0),
                             value.width, value.value.data(),
                             value.fourState ? value.unknown.data() : nullptr});
      }
    }
    if (physical != site.inputCount)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_format_env_v1 environment{
        scope, scopeSize,  library, librarySize, 0, precision, nullptr, 0,
        multiplier};
    return obelisk_rt_v1_display(context, static_cast<uint32_t>(*descriptor),
                                 newline, static_cast<obelisk_rt_radix>(radix),
                                 arguments.data(), arguments.size(),
                                 &environment);
  }
  case OBELISK_RT_INTRINSIC_V1_FINISH:
  case OBELISK_RT_INTRINSIC_V1_FATAL: {
    auto verbosity = scalar(0);
    if (!verbosity || *verbosity > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    return signature.id == OBELISK_RT_INTRINSIC_V1_FINISH
               ? obelisk_rt_v1_scheduler_finish(
                     context, static_cast<uint32_t>(*verbosity))
               : obelisk_rt_v1_scheduler_fatal(
                     context, static_cast<uint32_t>(*verbosity));
  }
  case OBELISK_RT_INTRINSIC_V1_TERMINATION_REQUESTED:
    return sentinel(0, obelisk_rt_v1_scheduler_termination_requested(context));
  case OBELISK_RT_INTRINSIC_V1_TIME_NOW:
    return sentinel(0, obelisk_rt_v1_scheduler_time(context));
  case OBELISK_RT_INTRINSIC_V1_TIME_TO_REAL: {
    auto ticks = scalar(0);
    auto scale = scalar(1);
    if (!ticks || !scale || *scale == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return writeReal(0,
                     static_cast<double>(*ticks) / static_cast<double>(*scale));
  }
  case OBELISK_RT_INTRINSIC_V1_TIME_FROM_REAL: {
    auto value = realInput(0);
    auto scale = scalar(1);
    auto quantum = scalar(2);
    if (!value || !scale || !quantum || *scale == 0 || *quantum == 0 ||
        *scale % *quantum != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    double nonnegative = *value >= 0.0 ? *value : 0.0;
    double steps = nonnegative * (static_cast<double>(*scale) /
                                  static_cast<double>(*quantum));
    double rounded = steps + 0.5;
    uint64_t maximumSteps = UINT64_MAX / *quantum;
    uint64_t tickSteps =
        !std::isfinite(rounded) || rounded >= std::ldexp(1.0, 64)
            ? maximumSteps
            : std::min(static_cast<uint64_t>(rounded), maximumSteps);
    return sentinel(0, tickSteps * *quantum);
  }
  case OBELISK_RT_INTRINSIC_V1_REAL_FROM_INTEGER: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Logic integer = readLogic(frame.data, input);
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (output.kind == OBELISK_RT_DBREG_REAL32) {
      float value = integerToFloat(std::move(integer), signature.flags != 0);
      std::memcpy(frame.data + output.offset, &value, sizeof(value));
      return OBELISK_RT_OK;
    }
    return writeReal(0,
                     integerToDouble(std::move(integer), signature.flags != 0));
  }
  case OBELISK_RT_INTRINSIC_V1_REAL_TO_INTEGER: {
    auto value = realInput(0);
    if (!value)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    Logic integer = doubleToInteger(*value, output.width);
    writeLogic(frame.data, output, integer);
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_REAL_COMPARE: {
    auto lhs = realInput(0);
    auto rhs = realInput(1);
    if (!lhs || !rhs)
      return OBELISK_RT_INVALID_BYTECODE;
    bool result;
    switch (signature.flags) {
    case 0:
      result = *lhs == *rhs;
      break;
    case 1:
      result = *lhs != *rhs;
      break;
    case 2:
      result = *lhs < *rhs;
      break;
    case 3:
      result = *lhs <= *rhs;
      break;
    case 4:
      result = *lhs > *rhs;
      break;
    case 5:
      result = *lhs >= *rhs;
      break;
    default:
      return OBELISK_RT_INVALID_BYTECODE;
    }
    return sentinel(0, result ? 1 : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_COUNT_BITS: {
    Logic input = readLogic(frame.data,
                            layoutAt(image, frame.function, inputRegister(0)));
    bool selected[4] = {};
    for (uint32_t index = 1; index != site.inputCount; ++index) {
      Logic control = readLogic(
          frame.data, layoutAt(image, frame.function, inputRegister(index)));
      unsigned state = (bit(control.unknown, 0) ? 2u : 0u) |
                       (bit(control.value, 0) ? 1u : 0u);
      selected[state] = true;
    }
    uint32_t count = 0;
    for (size_t index = 0; index != input.value.size(); ++index) {
      uint64_t value = input.value[index];
      uint64_t unknown = input.unknown[index];
      uint64_t known = ~unknown;
      uint64_t matches = 0;
      if (selected[0])
        matches |= ~value & known;
      if (selected[1])
        matches |= value & known;
      if (selected[2])
        matches |= ~value & unknown;
      if (selected[3])
        matches |= value & unknown;
      if (index + 1 == input.value.size())
        matches &= finalMask(input.width);
      while (matches) {
        matches &= matches - 1;
        ++count;
      }
    }
    return sentinel(0, count);
  }
  case OBELISK_RT_INTRINSIC_V1_CLOG2: {
    Logic input = readLogic(frame.data,
                            layoutAt(image, frame.function, inputRegister(0)));
    std::vector<uint64_t> value(input.value.size());
    bool nonzero = false;
    for (size_t index = 0; index != value.size(); ++index) {
      value[index] = input.value[index] & ~input.unknown[index];
      nonzero |= value[index] != 0;
    }
    if (!nonzero)
      return sentinel(0, 0);
    for (size_t index = 0; index != value.size(); ++index) {
      uint64_t previous = value[index];
      --value[index];
      if (previous != 0)
        break;
    }
    uint32_t result = 0;
    for (size_t index = value.size(); index != 0; --index) {
      uint64_t limb = value[index - 1];
      if (limb == 0)
        continue;
      unsigned activeBits = 0;
      while (limb) {
        ++activeBits;
        limb >>= 1;
      }
      result = static_cast<uint32_t>((index - 1) * 64 + activeBits);
      break;
    }
    return sentinel(0, result);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_MCD:
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN: {
    auto path = bytes(0);
    auto mode = signature.id == OBELISK_RT_INTRINSIC_V1_FILE_OPEN
                    ? bytes(1)
                    : std::optional<ByteSpan>{};
    if (!path || (signature.id == OBELISK_RT_INTRINSIC_V1_FILE_OPEN && !mode))
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t descriptor = 0;
    obelisk_rt_status status =
        signature.id == OBELISK_RT_INTRINSIC_V1_FILE_OPEN_MCD
            ? obelisk_rt_v1_file_open_mcd(
                  context, reinterpret_cast<const char *>(path->data),
                  path->size, &descriptor)
            : obelisk_rt_v1_file_open(
                  context, reinterpret_cast<const char *>(path->data),
                  path->size, reinterpret_cast<const char *>(mode->data),
                  mode->size, &descriptor);
    return sentinel(0, status == OBELISK_RT_OK ? descriptor : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING_MCD:
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING: {
    obelisk_rt_string_v1 path = 0;
    obelisk_rt_string_v1 mode = 0;
    bool withMode = signature.id == OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING;
    if (!readString(inputRegister(0), path) ||
        (withMode && !readString(inputRegister(1), mode)))
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t descriptor = 0;
    obelisk_rt_status status =
        withMode
            ? obelisk_rt_v1_file_open_string(context, path, mode, &descriptor)
            : obelisk_rt_v1_file_open_string_mcd(context, path, &descriptor);
    return sentinel(0, status == OBELISK_RT_OK ? descriptor : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_GETLINE_STRING: {
    auto descriptor = scalar(0);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 string = 0;
    uint32_t count = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_getline_string(
        context, lane, static_cast<uint32_t>(*descriptor), &string, &count);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(outputRegister(0), string))
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, count);
  }
  case OBELISK_RT_INTRINSIC_V1_TIME_FORMAT: {
    auto units = scalar(0), digits = scalar(1), width = scalar(3);
    auto suffix = bytes(2);
    if (!units || !digits || !width || !suffix)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_time_format(
        context, static_cast<int32_t>(*units), static_cast<uint32_t>(*digits),
        reinterpret_cast<const char *>(suffix->data), suffix->size,
        static_cast<uint32_t>(*width));
  }
  case OBELISK_RT_INTRINSIC_V1_PLUSARG_TEST: {
    obelisk_rt_string_v1 name = 0;
    if (!readString(inputRegister(0), name))
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t found = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_plusarg_test(context, name, &found);
    return sentinel(0, status == OBELISK_RT_OK ? found : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_PLUSARG_VALUE: {
    obelisk_rt_string_v1 prefix = 0;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), prefix))
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 tail = 0;
    uint32_t found = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_plusarg_value(context, lane, prefix, &tail, &found);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(outputRegister(0), tail))
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, found);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_ERROR_STRING: {
    auto descriptor = scalar(0);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 message = 0;
    int32_t code = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_error_string(
        context, lane, static_cast<uint32_t>(*descriptor), &message, &code);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(outputRegister(0), message))
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, static_cast<uint32_t>(code));
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_CLOSE:
  case OBELISK_RT_INTRINSIC_V1_FILE_FLUSH: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    return signature.id == OBELISK_RT_INTRINSIC_V1_FILE_CLOSE
               ? obelisk_rt_v1_file_close(context,
                                          static_cast<uint32_t>(*descriptor))
               : obelisk_rt_v1_file_flush(context,
                                          static_cast<uint32_t>(*descriptor));
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_GETC: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    uint8_t byte = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_getc(
        context, static_cast<uint32_t>(*descriptor), &byte);
    return sentinel(0, status == OBELISK_RT_OK ? byte : UINT32_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_UNGETC: {
    auto byte = scalar(0), descriptor = scalar(1);
    if (!byte || !descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_file_ungetc(context, static_cast<uint32_t>(*descriptor),
                                  static_cast<uint8_t>(*byte));
    return sentinel(0, status == OBELISK_RT_OK ? 0 : UINT32_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_GETLINE: {
    auto descriptor = scalar(0);
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_buffer_v1 line{};
    obelisk_rt_status status = obelisk_rt_v1_file_getline(
        context, static_cast<uint32_t>(*descriptor), output.width / 8, &line);
    bool packed =
        packBytes(image, frame, outputRegister(0), line.data, line.size, false);
    uint64_t count = status == OBELISK_RT_OK ? line.size : 0;
    obelisk_rt_v1_buffer_release(&line);
    if (!packed)
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, count);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_READ_PACKED: {
    auto descriptor = scalar(0);
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t capacity = (uint64_t{output.width} + 7) / 8;
    std::vector<uint8_t> data(static_cast<size_t>(capacity));
    uint64_t count = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_file_read(context, static_cast<uint32_t>(*descriptor),
                                data.data(), capacity, &count);
    if (!packBytes(image, frame, outputRegister(0), data.data(), count, true))
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, status == OBELISK_RT_OK ? count : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_EOF: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t eof = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_eof(
        context, static_cast<uint32_t>(*descriptor), &eof);
    return sentinel(0, status == OBELISK_RT_OK ? eof : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_SEEK: {
    auto descriptor = scalar(0), offset = scalar(1), origin = scalar(2);
    if (!descriptor || !offset || !origin || *descriptor > UINT32_MAX ||
        *origin > OBELISK_RT_SEEK_END)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_file_seek(context, static_cast<uint32_t>(*descriptor),
                                static_cast<int64_t>(*offset),
                                static_cast<obelisk_rt_seek_origin>(*origin));
    return sentinel(0, status == OBELISK_RT_OK ? 0 : UINT32_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_TELL: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    int64_t offset = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_tell(
        context, static_cast<uint32_t>(*descriptor), &offset);
    return sentinel(0, status == OBELISK_RT_OK ? static_cast<uint64_t>(offset)
                                               : UINT64_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_REWIND: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_file_rewind(context, static_cast<uint32_t>(*descriptor));
    return sentinel(0, status == OBELISK_RT_OK ? 0 : UINT32_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_ROOT: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    obelisk_rt_status status = obelisk_rt_cached_design_root(context, &cursor);
    if (!writeScalar(image, frame, outputRegister(0), cursor.offset))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_CHILD:
  case OBELISK_RT_INTRINSIC_V1_VPI_SIBLING: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{}, result{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        signature.id == OBELISK_RT_INTRINSIC_V1_VPI_CHILD
            ? obelisk_rt_cached_design_child(context, cursor, &result)
            : obelisk_rt_cached_design_sibling(context, cursor, &result);
    if (!writeScalar(image, frame, outputRegister(0), result.offset))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_CHILD_AT:
  case OBELISK_RT_INTRINSIC_V1_VPI_TYPE_CHILD: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{}, result{};
    auto index = scalar(1);
    if (!cursorInput(0, cursor) || !index)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        signature.id == OBELISK_RT_INTRINSIC_V1_VPI_CHILD_AT
            ? obelisk_rt_cached_design_child_at(context, cursor, *index,
                                                &result)
            : obelisk_rt_cached_design_type_child(context, cursor, *index,
                                                  &result);
    if (!writeScalar(image, frame, outputRegister(0), result.offset))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_LOOKUP: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    auto name = bytes(0);
    if (!name)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_design_cursor_v1 cursor{};
    obelisk_rt_status status = obelisk_rt_cached_design_lookup(
        context, name->data, name->size, &cursor);
    if (!writeScalar(image, frame, outputRegister(0), cursor.offset))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_INFO: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_design_info_v1 info{};
    obelisk_rt_status status =
        obelisk_rt_cached_design_info(context, cursor, &info);
    std::array<uint64_t, 7> outputs{info.kind,
                                    info.capabilities,
                                    info.handle.id,
                                    info.type_offset,
                                    static_cast<uint64_t>(info.range_left),
                                    static_cast<uint64_t>(info.range_right),
                                    info.bit_width};
    for (uint32_t index = 0; index != outputs.size(); ++index)
      if (!writeScalar(image, frame, outputRegister(index), outputs[index]))
        return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(7, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_NAME: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    const uint8_t *data = nullptr;
    uint64_t size = 0, offset = 0;
    obelisk_rt_status status =
        obelisk_rt_cached_design_name(context, cursor, &data, &size);
    if (status == OBELISK_RT_OK) {
      const uint8_t *begin = context->execution->design_database;
      uint64_t databaseSize = context->execution->design_database_size;
      uintptr_t dataAddress = reinterpret_cast<uintptr_t>(data);
      uintptr_t beginAddress = reinterpret_cast<uintptr_t>(begin);
      if (dataAddress < beginAddress ||
          dataAddress - beginAddress > databaseSize ||
          size > databaseSize - (dataAddress - beginAddress))
        status = OBELISK_RT_INVALID_DESIGN;
      else
        offset = dataAddress - beginAddress;
    }
    if (!writeScalar(image, frame, outputRegister(0), offset) ||
        !writeScalar(image, frame, outputRegister(1), size))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(2, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_TYPE_INFO: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_design_type_info_v1 info{};
    obelisk_rt_status status =
        obelisk_rt_cached_design_type_info(context, cursor, &info);
    std::array<uint64_t, 11> outputs{info.kind,
                                     info.flags,
                                     info.bit_width,
                                     static_cast<uint64_t>(info.range_left),
                                     static_cast<uint64_t>(info.range_right),
                                     info.element_type.offset,
                                     info.first_child.offset,
                                     info.child_count,
                                     info.ordinal,
                                     info.tag_bits,
                                     info.packed_offset};
    for (uint32_t index = 0; index != outputs.size(); ++index)
      if (!writeScalar(image, frame, outputRegister(index), outputs[index]))
        return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(11, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_READ: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    Logic value{output.width, output.kind == OBELISK_RT_DBREG_LOGIC,
                LimbVector(limbCount(output.width)),
                LimbVector(limbCount(output.width))};
    obelisk_rt_status status = obelisk_rt_v1_design_read(
        context, cursor, value.value.data(), value.unknown.data(), value.width);
    writeLogic(frame.data, output, value);
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_WRITE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    Logic value = readLogic(frame.data,
                            layoutAt(image, frame.function, inputRegister(1)));
    obelisk_rt_status status = obelisk_rt_v1_design_write(
        context, cursor, value.value.data(),
        value.fourState ? value.unknown.data() : nullptr, value.width);
    return finishVPI(0, status);
  }
  default:
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

} // namespace obelisk::designbytecode
