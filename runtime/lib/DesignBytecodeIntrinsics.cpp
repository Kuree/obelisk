//===- DesignBytecodeIntrinsics.cpp - Runtime service dispatch ----------===//

#include "DesignBytecodeExecution.h"
#include "DesignBytecodeNets.h"
#include "RuntimeInternal.h"
#include "obelisk/Runtime/OutputItemFlags.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

decltype(&obelisk_rt_v1_dpi_open_array_aggregate_pack)
    designBytecodeDpiOpenAggregatePack = nullptr;
decltype(&obelisk_rt_v1_dpi_open_array_aggregate_unpack)
    designBytecodeDpiOpenAggregateUnpack = nullptr;
decltype(&obelisk_rt_v1_dpi_aggregate_pack) designBytecodeDpiAggregatePack =
    nullptr;
decltype(&obelisk_rt_v1_dpi_aggregate_unpack) designBytecodeDpiAggregateUnpack =
    nullptr;
decltype(&obelisk_rt_v1_dpi_open_array_aggregate_roots_push)
    designBytecodeDpiOpenAggregateRootsPush = nullptr;
decltype(&obelisk_rt_v1_dpi_aggregate_roots_pop)
    designBytecodeDpiAggregateRootsPop = nullptr;
decltype(&obelisk_rt_v1_dpi_open_array_prepare_recursive)
    designBytecodeDpiOpenPrepareRecursive = nullptr;
decltype(&obelisk_rt_v1_dpi_open_array_finish_recursive)
    designBytecodeDpiOpenFinishRecursive = nullptr;

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
    obelisk_rt_managed_word_v1 word = 0;
    std::memcpy(&word, frame.data + layout.offset, sizeof(word));
    return obelisk_rt_object_from_managed_word(word);
  };
  auto writeManaged = [&](uint32_t reg, obelisk_rt_object_v1 *object) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8)
      return false;
    obelisk_rt_managed_word_v1 word =
        obelisk_rt_managed_word_from_object(object);
    std::memcpy(frame.data + layout.offset, &word, sizeof(word));
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
    obelisk_rt_managed_word_v1 word = 0;
    std::memcpy(&word, frame.data + layout.offset, sizeof(word));
    object = obelisk_rt_object_from_managed_word(word);
    std::memcpy(&offset, frame.data + layout.offset + 8, sizeof(offset));
    return true;
  };
  auto readArgumentRef = [&](uint32_t reg, obelisk_rt_object_v1 *&owner,
                             uint64_t &payload, uint32_t &managed) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_ARGUMENT_REF || layout.size != 24)
      return false;
    obelisk_rt_managed_word_v1 ownerWord = 0;
    std::memcpy(&ownerWord, frame.data + layout.offset, sizeof(ownerWord));
    owner = obelisk_rt_object_from_managed_word(ownerWord);
    std::memcpy(&payload, frame.data + layout.offset + 8, sizeof(payload));
    uint64_t tag = 0;
    std::memcpy(&tag, frame.data + layout.offset + 16, sizeof(tag));
    if (tag > 2)
      return false;
    managed = static_cast<uint32_t>(tag);
    return true;
  };
  std::vector<uint8_t> assocValueScratch;
  std::vector<uint8_t> assocUnknownScratch;
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
    if (layout.kind == OBELISK_RT_DBREG_MANAGED) {
      if ((key.kind != OBELISK_RT_ASSOC_KEY_CLASS &&
           key.kind != OBELISK_RT_ASSOC_KEY_WILDCARD) ||
          layout.size != 8)
        return false;
      obelisk_rt_managed_word_v1 word = 0;
      std::memcpy(&word, frame.data + layout.offset, sizeof(word));
      key.object = obelisk_rt_object_from_managed_word(word);
      return true;
    }
    if (key.kind == OBELISK_RT_ASSOC_KEY_PROCESS) {
      if (layout.kind != OBELISK_RT_DBREG_BITS || layout.size != 8 ||
          layout.width != 64)
        return false;
      std::memcpy(&key.value, frame.data + layout.offset, sizeof(key.value));
      return true;
    }
    if ((layout.kind != OBELISK_RT_DBREG_BITS &&
         layout.kind != OBELISK_RT_DBREG_LOGIC) ||
        key.kind == OBELISK_RT_ASSOC_KEY_STRING ||
        key.kind == OBELISK_RT_ASSOC_KEY_CLASS || layout.width != key.width ||
        layout.width == 0)
      return false;
    uint64_t planeSize =
        layout.kind == OBELISK_RT_DBREG_LOGIC ? layout.size / 2 : layout.size;
    if (planeSize == 0)
      return false;
    if (key.width <= 64) {
      std::memcpy(&key.value, frame.data + layout.offset,
                  static_cast<size_t>(planeSize));
      if (layout.kind == OBELISK_RT_DBREG_LOGIC)
        std::memcpy(&key.unknown, frame.data + layout.offset + planeSize,
                    static_cast<size_t>(planeSize));
    } else {
      OBELISK_RT_TRY {
        assocValueScratch.assign(frame.data + layout.offset,
                                 frame.data + layout.offset + planeSize);
        assocUnknownScratch.clear();
        if (layout.kind == OBELISK_RT_DBREG_LOGIC)
          assocUnknownScratch.assign(frame.data + layout.offset + planeSize,
                                     frame.data + layout.offset +
                                         2 * planeSize);
      }
      OBELISK_RT_CATCH(const std::bad_alloc &) { return false; }
      key.value_data = assocValueScratch.data();
      if (!assocUnknownScratch.empty())
        key.unknown_data = assocUnknownScratch.data();
    }
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
    if (layout.kind == OBELISK_RT_DBREG_MANAGED) {
      if ((key.kind != OBELISK_RT_ASSOC_KEY_CLASS &&
           key.kind != OBELISK_RT_ASSOC_KEY_WILDCARD) ||
          layout.size != 8)
        return false;
      obelisk_rt_managed_word_v1 word =
          obelisk_rt_managed_word_from_object(key.object);
      std::memcpy(frame.data + layout.offset, &word, sizeof(word));
      return true;
    }
    if (key.kind == OBELISK_RT_ASSOC_KEY_PROCESS) {
      if (layout.kind != OBELISK_RT_DBREG_BITS || layout.size != 8 ||
          layout.width != 64)
        return false;
      std::memcpy(frame.data + layout.offset, &key.value, sizeof(key.value));
      return true;
    }
    if ((layout.kind != OBELISK_RT_DBREG_BITS &&
         layout.kind != OBELISK_RT_DBREG_LOGIC) ||
        key.kind == OBELISK_RT_ASSOC_KEY_STRING ||
        key.kind == OBELISK_RT_ASSOC_KEY_CLASS || layout.width != key.width)
      return false;
    uint64_t planeSize =
        layout.kind == OBELISK_RT_DBREG_LOGIC ? layout.size / 2 : layout.size;
    if (planeSize == 0 || (key.width > 64 && !key.value_data))
      return false;
    if (key.width <= 64) {
      std::memcpy(frame.data + layout.offset, &key.value,
                  static_cast<size_t>(planeSize));
      if (layout.kind == OBELISK_RT_DBREG_LOGIC)
        std::memcpy(frame.data + layout.offset + planeSize, &key.unknown,
                    static_cast<size_t>(planeSize));
    } else {
      std::memcpy(frame.data + layout.offset, key.value_data,
                  static_cast<size_t>(planeSize));
      if (layout.kind == OBELISK_RT_DBREG_LOGIC && key.unknown_data)
        std::memcpy(frame.data + layout.offset + planeSize, key.unknown_data,
                    static_cast<size_t>(planeSize));
    }
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
    uint64_t storagePlaneSize =
        output.kind == OBELISK_RT_DBREG_LOGIC ? output.size / 2 : output.size;
    if (output.kind == OBELISK_RT_DBREG_LOGIC)
      std::memset(frame.data + output.offset + storagePlaneSize, 0xff,
                  storagePlaneSize);
    void *unknown = output.kind == OBELISK_RT_DBREG_LOGIC
                        ? frame.data + output.offset + storagePlaneSize
                        : nullptr;
    return obelisk_rt_v1_container_read_checked(
        readManaged(inputRegister(0)), static_cast<int64_t>(*index),
        frame.data + output.offset, storagePlaneSize, unknown,
        unknown ? storagePlaneSize : 0);
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
    uint64_t storagePlaneSize =
        input.kind == OBELISK_RT_DBREG_LOGIC ? input.size / 2 : input.size;
    const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                              ? frame.data + input.offset + storagePlaneSize
                              : nullptr;
    return obelisk_rt_v1_container_write_checked(
        lane, readManaged(inputRegister(0)), static_cast<int64_t>(*index),
        frame.data + input.offset, storagePlaneSize, unknown,
        unknown ? storagePlaneSize : 0);
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
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_IMPORT_FIXED: {
    std::array<std::optional<uint64_t>, 5> inputs;
    for (uint32_t index = 2; index != 7; ++index)
      inputs[index - 2] = scalar(index);
    if (std::any_of(inputs.begin(), inputs.end(),
                    [](const auto &value) { return !value; }))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    bool fourState = input.kind == OBELISK_RT_DBREG_LOGIC;
    uint64_t framePlaneSize = fourState ? input.size / 2 : input.size;
    if (*inputs[0] > framePlaneSize || *inputs[2] != fourState)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    const void *unknown =
        fourState ? frame.data + input.offset + framePlaneSize : nullptr;
    return obelisk_rt_v1_container_import_fixed(
        lane, readManaged(inputRegister(0)), frame.data + input.offset, unknown,
        *inputs[0], *inputs[1], fourState, *inputs[3], *inputs[4]);
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_EXPORT_FIXED: {
    std::array<std::optional<uint64_t>, 5> inputs;
    for (uint32_t index = 1; index != 6; ++index)
      inputs[index - 1] = scalar(index);
    if (std::any_of(inputs.begin(), inputs.end(),
                    [](const auto &value) { return !value; }))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    bool fourState = output.kind == OBELISK_RT_DBREG_LOGIC;
    uint64_t framePlaneSize = fourState ? output.size / 2 : output.size;
    if (*inputs[0] > framePlaneSize || *inputs[2] != fourState)
      return OBELISK_RT_INVALID_BYTECODE;
    std::memset(frame.data + output.offset, 0, output.size);
    void *unknown =
        fourState ? frame.data + output.offset + framePlaneSize : nullptr;
    return obelisk_rt_v1_container_export_fixed(
        readManaged(inputRegister(0)), frame.data + output.offset, unknown,
        *inputs[0], *inputs[1], fourState, *inputs[3], *inputs[4]);
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_SWAP: {
    auto left = scalar(1);
    auto right = scalar(2);
    return left && right
               ? obelisk_rt_v1_container_swap(readManaged(inputRegister(0)),
                                              static_cast<int64_t>(*left),
                                              static_cast<int64_t>(*right))
               : OBELISK_RT_INVALID_BYTECODE;
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
  case OBELISK_RT_INTRINSIC_V1_MAILBOX_CREATE: {
    std::array<std::optional<uint64_t>, 7> inputs;
    for (uint32_t index = 0; index != 6; ++index)
      inputs[index] = scalar(index);
    inputs[6] = scalar(7);
    if (std::any_of(inputs.begin(), inputs.end(),
                    [](const auto &value) { return !value; }))
      return OBELISK_RT_INVALID_BYTECODE;
    std::optional<ByteSpan> trace =
        readByteSpan(image, frame, inputRegister(6));
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
    obelisk_rt_status status = obelisk_rt_v1_mailbox_create_typed(
        lane, *inputs[0], static_cast<uint32_t>(*inputs[1]),
        static_cast<uint32_t>(*inputs[2]), *inputs[3], *inputs[4], *inputs[5],
        traceSlots.data(), traceSlots.size(), static_cast<int64_t>(*inputs[6]),
        &result);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_MAILBOX_NUM: {
    uint32_t count = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_mailbox_num(readManaged(inputRegister(0)), &count);
    return status == OBELISK_RT_OK ? sentinel(0, count) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_BOX_IS_TYPE: {
    auto typeID = scalar(1);
    return typeID ? sentinel(0, obelisk_rt_v1_box_is_type(
                                    readManaged(inputRegister(0)), *typeID))
                  : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_MAILBOX_TRY_PUT: {
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    uint32_t success = 0;
    obelisk_rt_status status;
    if (input.kind == OBELISK_RT_DBREG_HANDLE) {
      int64_t event = kInvalidHandleStart;
      std::memcpy(&event, frame.data + input.offset + 16, sizeof(event));
      status = obelisk_rt_v1_mailbox_try_put_checked(
          lane, readManaged(inputRegister(0)), &event, sizeof(event), nullptr,
          0, &success);
    } else {
      uint64_t planeSize =
          input.kind == OBELISK_RT_DBREG_LOGIC ? input.size / 2 : input.size;
      const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                                ? frame.data + input.offset + planeSize
                                : nullptr;
      status = obelisk_rt_v1_mailbox_try_put_checked(
          lane, readManaged(inputRegister(0)), frame.data + input.offset,
          planeSize, unknown, unknown ? planeSize : 0, &success);
    }
    return status == OBELISK_RT_OK ? sentinel(0, success) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_MAILBOX_TRY_PEEK:
  case OBELISK_RT_INTRINSIC_V1_MAILBOX_TRY_GET: {
    Layout output = layoutAt(image, frame.function, outputRegister(1));
    std::memset(frame.data + output.offset, 0, output.size);
    uint32_t present = 0;
    bool remove = signature.id == OBELISK_RT_INTRINSIC_V1_MAILBOX_TRY_GET;
    obelisk_rt_status status;
    if (output.kind == OBELISK_RT_DBREG_HANDLE) {
      uint64_t event = UINT64_MAX;
      status = remove ? obelisk_rt_v1_mailbox_try_get_checked(
                            readManaged(inputRegister(0)), &event,
                            sizeof(event), nullptr, 0, &present)
                      : obelisk_rt_v1_mailbox_try_peek_checked(
                            readManaged(inputRegister(0)), &event,
                            sizeof(event), nullptr, 0, &present);
      if (status == OBELISK_RT_OK) {
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
      }
    } else {
      uint64_t planeSize =
          output.kind == OBELISK_RT_DBREG_LOGIC ? output.size / 2 : output.size;
      void *unknown = output.kind == OBELISK_RT_DBREG_LOGIC
                          ? frame.data + output.offset + planeSize
                          : nullptr;
      status =
          remove
              ? obelisk_rt_v1_mailbox_try_get_checked(
                    readManaged(inputRegister(0)), frame.data + output.offset,
                    planeSize, unknown, unknown ? planeSize : 0, &present)
              : obelisk_rt_v1_mailbox_try_peek_checked(
                    readManaged(inputRegister(0)), frame.data + output.offset,
                    planeSize, unknown, unknown ? planeSize : 0, &present);
    }
    return status == OBELISK_RT_OK ? sentinel(0, present) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_SEMAPHORE_CREATE: {
    std::optional<uint64_t> keys = scalar(0);
    if (!keys)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *result = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_semaphore_create(
        lane, static_cast<int32_t>(*keys), &result);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_SEMAPHORE_PUT: {
    std::optional<uint64_t> keys = scalar(1);
    return keys ? obelisk_rt_v1_semaphore_put(readManaged(inputRegister(0)),
                                              static_cast<int32_t>(*keys))
                : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_SEMAPHORE_TRY_GET: {
    std::optional<uint64_t> keys = scalar(1);
    if (!keys)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t success = 0;
    obelisk_rt_status status = obelisk_rt_v1_semaphore_try_get(
        readManaged(inputRegister(0)), static_cast<int32_t>(*keys), &success);
    return status == OBELISK_RT_OK ? sentinel(0, success) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_EVENT_CREATE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    uint64_t stableID = UINT64_MAX;
    obelisk_rt_status status =
        obelisk_rt_v1_scheduler_event_create(context, &stableID);
    if (status != OBELISK_RT_OK)
      return status;
    uint32_t kind = OBELISK_RT_DESCRIPTOR_EVENT;
    int64_t start = static_cast<int64_t>(stableID);
    int64_t end = start == INT64_MAX ? start : start + 1;
    std::memset(frame.data + output.offset, 0, output.size);
    std::memcpy(frame.data + output.offset, &kind, sizeof(kind));
    std::memcpy(frame.data + output.offset + 8, &start, sizeof(start));
    std::memcpy(frame.data + output.offset + 16, &start, sizeof(start));
    std::memcpy(frame.data + output.offset + 24, &end, sizeof(end));
    return OBELISK_RT_OK;
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
    uint8_t *stateValue =
        context->stateValue.empty()
            ? nullptr
            : reinterpret_cast<uint8_t *>(context->stateValue.data());
    uint8_t *stateUnknown =
        context->stateUnknown.empty()
            ? nullptr
            : reinterpret_cast<uint8_t *>(context->stateUnknown.data());
    obelisk_rt_status status = obelisk_rt_v1_reference_path_assoc_create(
        lane, array, &key, watchOwner, ownerPayload, ownerManaged, stateValue,
        stateUnknown, image.stateBitCount, &path);
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
    auto distribution = scalar(0), seed = scalar(1), first = scalar(2),
         second = scalar(3);
    if (!distribution || !seed || !first || !second ||
        *distribution > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    int32_t result = 0;
    int32_t nextSeed = 0;
    obelisk_rt_status status = obelisk_rt_v1_random_distribution(
        context, static_cast<obelisk_rt_distribution>(*distribution),
        static_cast<int32_t>(*seed), static_cast<int32_t>(*first),
        static_cast<int32_t>(*second), &result, &nextSeed);
    if (status != OBELISK_RT_OK ||
        !writeScalar(image, frame, outputRegister(0),
                     static_cast<uint32_t>(result)) ||
        !writeScalar(image, frame, outputRegister(1),
                     static_cast<uint32_t>(nextSeed)))
      return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_BYTECODE : status;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_CYCLE_NEXT: {
    auto key = scalar(0), position = scalar(1), width = scalar(2);
    if (!key || !position || !width || *width > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t nextPosition = 0;
    uint64_t value = 0;
    obelisk_rt_status status = obelisk_rt_v1_random_cycle_next(
        *key, *position, static_cast<uint32_t>(*width), &nextPosition, &value);
    if (status != OBELISK_RT_OK ||
        !writeScalar(image, frame, outputRegister(0), nextPosition) ||
        !writeScalar(image, frame, outputRegister(1), value))
      return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_BYTECODE : status;
    return OBELISK_RT_OK;
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
  case OBELISK_RT_INTRINSIC_V1_RANDOM_GET_STATE: {
    obelisk_rt_random_state_v1 state{};
    obelisk_rt_status status = obelisk_rt_v1_random_get_state(context, &state);
    if (status != OBELISK_RT_OK)
      return status;
    status = sentinel(0, state.state);
    return status == OBELISK_RT_OK ? sentinel(1, state.increment) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SET_STATE: {
    auto state = scalar(0);
    auto increment = scalar(1);
    if (!state || !increment)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_random_state_v1 snapshot{*state, *increment};
    return obelisk_rt_v1_random_set_state(context, &snapshot);
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE: {
    std::optional<ByteSpan> program = bytes(0);
    auto start = scalar(1), mutableMask = scalar(2), constraintMask = scalar(3),
         maxAttempts = scalar(4);
    if (!program || !start || !mutableMask || !constraintMask || !maxAttempts)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint64_t> captures;
    captures.reserve(site.inputCount - 5);
    for (uint32_t index = 5; index != site.inputCount; ++index) {
      auto capture = scalar(index);
      if (!capture)
        return OBELISK_RT_INVALID_BYTECODE;
      captures.push_back(*capture);
    }
    uint64_t assignment = 0;
    uint32_t success = 0;
    obelisk_rt_status status = obelisk_rt_v1_random_solve_modes(
        context, program->data, program->size, *start, *mutableMask,
        *constraintMask, *maxAttempts, captures.data(), captures.size(),
        &assignment, &success);
    if (status != OBELISK_RT_OK ||
        !writeScalar(image, frame, outputRegister(0), assignment) ||
        !writeScalar(image, frame, outputRegister(1), success))
      return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_BYTECODE : status;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE_STATE: {
    std::optional<ByteSpan> program = bytes(0);
    auto start = scalar(1), mutableMask = scalar(2), constraintMask = scalar(3),
         maxAttempts = scalar(4), rngState = scalar(5),
         rngIncrement = scalar(6);
    if (!program || !start || !mutableMask || !constraintMask || !maxAttempts ||
        !rngState || !rngIncrement)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint64_t> captures;
    captures.reserve(site.inputCount - 7);
    for (uint32_t index = 7; index != site.inputCount; ++index) {
      auto capture = scalar(index);
      if (!capture)
        return OBELISK_RT_INVALID_BYTECODE;
      captures.push_back(*capture);
    }
    uint64_t assignment = 0;
    uint64_t nextRngState = 0;
    uint32_t success = 0;
    obelisk_rt_status status = obelisk_rt_v1_random_solve_modes_state(
        context, program->data, program->size, *start, *mutableMask,
        *constraintMask, *maxAttempts, *rngState, *rngIncrement,
        captures.data(), captures.size(), &assignment, &success, &nextRngState);
    if (status != OBELISK_RT_OK ||
        !writeScalar(image, frame, outputRegister(0), assignment) ||
        !writeScalar(image, frame, outputRegister(1), success) ||
        !writeScalar(image, frame, outputRegister(2), nextRngState))
      return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_BYTECODE : status;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE_WIDE_STATE: {
    std::optional<ByteSpan> program = bytes(0);
    auto constraintMask = scalar(3), maxAttempts = scalar(4),
         rngState = scalar(5), rngIncrement = scalar(6);
    if (!program || !constraintMask || !maxAttempts || !rngState ||
        !rngIncrement)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout startLayout = layoutAt(image, frame.function, inputRegister(1));
    Layout mutableLayout = layoutAt(image, frame.function, inputRegister(2));
    Layout assignmentLayout =
        layoutAt(image, frame.function, outputRegister(0));
    if (startLayout.kind != OBELISK_RT_DBREG_BITS ||
        mutableLayout.kind != OBELISK_RT_DBREG_BITS ||
        mutableLayout.width != startLayout.width ||
        assignmentLayout.kind != OBELISK_RT_DBREG_BITS ||
        assignmentLayout.width != startLayout.width)
      return OBELISK_RT_INVALID_BYTECODE;
    Logic start = readLogic(frame.data, startLayout);
    Logic mutableMask = readLogic(frame.data, mutableLayout);
    std::vector<uint64_t> captures;
    std::vector<uint32_t> captureWidths;
    OBELISK_RT_TRY {
      for (uint32_t index = 7; index != site.inputCount; ++index) {
        Layout layout = layoutAt(image, frame.function, inputRegister(index));
        if (layout.kind != OBELISK_RT_DBREG_BITS)
          return OBELISK_RT_INVALID_BYTECODE;
        captureWidths.push_back(layout.width);
        Logic capture = readLogic(frame.data, layout);
        captures.insert(captures.end(), capture.value.begin(),
                        capture.value.end());
      }
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    OBELISK_RT_CATCH(const std::length_error &) {
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    Logic assignment{start.width, false, LimbVector(limbCount(start.width)),
                     LimbVector(limbCount(start.width))};
    uint64_t nextRngState = 0;
    uint32_t success = 0;
    obelisk_rt_status status = obelisk_rt_v1_random_solve_wide_modes_state(
        context, program->data, program->size, start.value.data(),
        mutableMask.value.data(), start.value.size(), *constraintMask,
        *maxAttempts, *rngState, *rngIncrement, captures.data(),
        captures.size(), captureWidths.data(), captureWidths.size(),
        assignment.value.data(), &success, &nextRngState);
    if (status != OBELISK_RT_OK)
      return status;
    writeLogic(frame.data, assignmentLayout, assignment);
    if (!writeScalar(image, frame, outputRegister(1), success) ||
        !writeScalar(image, frame, outputRegister(2), nextRngState))
      return OBELISK_RT_INVALID_BYTECODE;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_CREATE: {
    auto type = scalar(0);
    if (!type || site.inputCount < 2)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint64_t> bins;
    OBELISK_RT_TRY {
      bins.reserve(site.inputCount - 1);
      for (uint32_t index = 1; index != site.inputCount; ++index) {
        auto count = scalar(index);
        if (!count)
          return OBELISK_RT_INVALID_BYTECODE;
        bins.push_back(*count);
      }
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    OBELISK_RT_CATCH(const std::length_error &) {
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
    if (!handle || site.inputCount < 1)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint8_t> hits;
    OBELISK_RT_TRY {
      hits.reserve(site.inputCount - 1);
      for (uint32_t index = 1; index != site.inputCount; ++index) {
        auto hit = scalar(index);
        if (!hit || *hit > 1)
          return OBELISK_RT_INVALID_BYTECODE;
        hits.push_back(static_cast<uint8_t>(*hit));
      }
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    OBELISK_RT_CATCH(const std::length_error &) {
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
    OBELISK_RT_TRY {
      bins.reserve(site.inputCount - 1);
      for (uint32_t index = 1; index != site.inputCount; ++index) {
        auto count = scalar(index);
        if (!count)
          return OBELISK_RT_INVALID_BYTECODE;
        bins.push_back(*count);
      }
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    OBELISK_RT_CATCH(const std::length_error &) {
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
    obelisk_rt_status status = obelisk_rt_v1_string_to_packed(
        input, frame.data + output.offset, unknown, output.width);
    if (status != OBELISK_RT_OK || signature.flags == 0)
      return status;
    return sentinel(1, obelisk_rt_v1_string_length(input) == output.width / 8);
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
    auto cursor = scalar(1), specifier = scalar(3), width = scalar(4);
    auto prefix = bytes(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !cursor || !specifier ||
        !width || !prefix || *cursor > UINT32_MAX || *specifier > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 field = 0;
    uint32_t nextCursor = 0;
    uint32_t ok = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_scan_field(
        lane, input, static_cast<uint32_t>(*cursor),
        reinterpret_cast<const char *>(prefix->data), prefix->size,
        static_cast<uint32_t>(*specifier), *width, &field, &nextCursor, &ok);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(outputRegister(0), field))
      return OBELISK_RT_INVALID_BYTECODE;
    status = sentinel(1, nextCursor);
    return status == OBELISK_RT_OK ? sentinel(2, ok) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_SCAN_FIELD: {
    auto descriptor = scalar(0), enabled = scalar(1), specifier = scalar(3),
         width = scalar(4);
    auto prefix = bytes(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!descriptor || !enabled || !specifier || !width || !prefix ||
        *descriptor > UINT32_MAX || *enabled > 1 || *specifier > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 field = 0;
    uint32_t ok = 0;
    uint32_t eof = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_scan_field(
        context, lane, static_cast<uint32_t>(*descriptor),
        static_cast<uint32_t>(*enabled),
        reinterpret_cast<const char *>(prefix->data), prefix->size,
        static_cast<uint32_t>(*specifier), *width, &field, &ok, &eof);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(outputRegister(0), field))
      return OBELISK_RT_INVALID_BYTECODE;
    status = sentinel(1, ok);
    return status == OBELISK_RT_OK ? sentinel(2, eof) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_SCAN_RAW: {
    obelisk_rt_string_v1 input = 0;
    auto cursor = scalar(1), rawSize = scalar(3), bitWidth = scalar(4),
         fourState = scalar(5), maxWidth = scalar(6);
    auto prefix = bytes(2);
    if (!readString(inputRegister(0), input) || !cursor || !rawSize ||
        !bitWidth || !fourState || !maxWidth || !prefix ||
        *cursor > UINT32_MAX || *fourState > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    bool assigned = site.outputCount == 3;
    void *value = nullptr;
    void *unknown = nullptr;
    uint64_t planeSize = 0;
    if (assigned) {
      Layout output = layoutAt(image, frame.function, outputRegister(0));
      if (output.kind != OBELISK_RT_DBREG_LOGIC || output.width == 0 ||
          output.width != *bitWidth || output.size % 2 != 0)
        return OBELISK_RT_INVALID_BYTECODE;
      planeSize = output.size / 2;
      value = frame.data + output.offset;
      unknown = frame.data + output.offset + planeSize;
    } else if (*bitWidth != 0 || *fourState != 0 || *maxWidth != *rawSize) {
      return OBELISK_RT_INVALID_BYTECODE;
    }
    uint32_t nextCursor = 0;
    uint32_t ok = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_scan_raw(
        input, static_cast<uint32_t>(*cursor),
        reinterpret_cast<const char *>(prefix->data), prefix->size, *rawSize,
        *bitWidth, static_cast<uint32_t>(*fourState), *maxWidth, value,
        planeSize, unknown, planeSize, &nextCursor, &ok);
    if (status != OBELISK_RT_OK)
      return status;
    uint32_t cursorOutput = assigned ? 1 : 0;
    status = sentinel(cursorOutput, nextCursor);
    return status == OBELISK_RT_OK ? sentinel(cursorOutput + 1, ok) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_SCAN_RAW: {
    auto descriptor = scalar(0), enabled = scalar(1), rawSize = scalar(3),
         bitWidth = scalar(4), fourState = scalar(5), maxWidth = scalar(6);
    auto prefix = bytes(2);
    if (!descriptor || !enabled || !rawSize || !bitWidth || !fourState ||
        !maxWidth || !prefix || *descriptor > UINT32_MAX || *enabled > 1 ||
        *fourState > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    bool assigned = site.outputCount == 3;
    void *value = nullptr;
    void *unknown = nullptr;
    uint64_t planeSize = 0;
    if (assigned) {
      Layout output = layoutAt(image, frame.function, outputRegister(0));
      if (output.kind != OBELISK_RT_DBREG_LOGIC || output.width == 0 ||
          output.width != *bitWidth || output.size % 2 != 0)
        return OBELISK_RT_INVALID_BYTECODE;
      planeSize = output.size / 2;
      value = frame.data + output.offset;
      unknown = frame.data + output.offset + planeSize;
    } else if (*bitWidth != 0 || *fourState != 0 || *maxWidth != *rawSize) {
      return OBELISK_RT_INVALID_BYTECODE;
    }
    uint32_t ok = 0;
    uint32_t eof = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_scan_raw(
        context, static_cast<uint32_t>(*descriptor),
        static_cast<uint32_t>(*enabled),
        reinterpret_cast<const char *>(prefix->data), prefix->size, *rawSize,
        *bitWidth, static_cast<uint32_t>(*fourState), *maxWidth, value,
        planeSize, unknown, planeSize, &ok, &eof);
    if (status != OBELISK_RT_OK)
      return status;
    uint32_t okOutput = assigned ? 1 : 0;
    status = sentinel(okOutput, ok);
    return status == OBELISK_RT_OK ? sentinel(okOutput + 1, eof) : status;
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
  case OBELISK_RT_INTRINSIC_V1_STRING_PARSE_LOGIC: {
    obelisk_rt_string_v1 input = 0;
    auto radix = scalar(1);
    if (!readString(inputRegister(0), input) || !radix)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (output.kind != OBELISK_RT_DBREG_LOGIC || output.width != 64)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t planeSize = ((uint64_t{output.width} + 63) / 64) * 8;
    return obelisk_rt_v1_string_parse_logic(
        input, static_cast<uint32_t>(*radix),
        reinterpret_cast<uint64_t *>(frame.data + output.offset),
        reinterpret_cast<uint64_t *>(frame.data + output.offset + planeSize));
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
    obelisk_rt_managed_word_v1 word =
        obelisk_rt_managed_word_from_object(object);
    std::memcpy(frame.data + output.offset, &word, sizeof(word));
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
    obelisk_rt_managed_word_v1 ownerWord = 0;
    std::memcpy(&ownerWord, frame.data + ownerReference.offset,
                sizeof(ownerWord));
    watchOwner = obelisk_rt_object_from_managed_word(ownerWord);
    std::memcpy(&ownerPayload, frame.data + ownerReference.offset + 8,
                sizeof(ownerPayload));
    std::memcpy(&ownerManaged, frame.data + ownerReference.offset + 16,
                sizeof(ownerManaged));
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *path = nullptr;
    uint8_t *stateValue =
        context->stateValue.empty()
            ? nullptr
            : reinterpret_cast<uint8_t *>(context->stateValue.data());
    uint8_t *stateUnknown =
        context->stateUnknown.empty()
            ? nullptr
            : reinterpret_cast<uint8_t *>(context->stateUnknown.data());
    obelisk_rt_status status = obelisk_rt_v1_reference_path_index_create(
        lane, readManaged(inputRegister(0)), static_cast<int64_t>(*index),
        watchOwner, ownerPayload, ownerManaged, stateValue, stateUnknown,
        image.stateBitCount, &path);
    if (status != OBELISK_RT_OK)
      return status;
    return writeManaged(outputRegister(0), path) ? OBELISK_RT_OK
                                                 : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_STRING_CHARACTER: {
    auto index = scalar(1);
    obelisk_rt_object_v1 *watchOwner = nullptr;
    uint64_t ownerPayload = 0;
    uint32_t ownerManaged = 0;
    if (!index || !readArgumentRef(inputRegister(2), watchOwner, ownerPayload,
                                   ownerManaged))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *path = nullptr;
    obelisk_rt_string_v1 string = 0;
    if (!readString(inputRegister(0), string))
      return OBELISK_RT_INVALID_BYTECODE;
    uint8_t *stateValue =
        context->stateValue.empty()
            ? nullptr
            : reinterpret_cast<uint8_t *>(context->stateValue.data());
    uint8_t *stateUnknown =
        context->stateUnknown.empty()
            ? nullptr
            : reinterpret_cast<uint8_t *>(context->stateUnknown.data());
    obelisk_rt_status status =
        obelisk_rt_v1_reference_path_string_character_create(
            lane, string, static_cast<int64_t>(*index), watchOwner,
            ownerPayload, ownerManaged, stateValue, stateUnknown,
            image.stateBitCount, &path);
    if (status != OBELISK_RT_OK)
      return status;
    return writeManaged(outputRegister(0), path) ? OBELISK_RT_OK
                                                 : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_AGGREGATE_ELEMENT: {
    std::array<std::optional<uint64_t>, 13> inputs;
    inputs[0] = scalar(1);
    for (uint32_t index = 2; index != 14; ++index)
      inputs[index - 1] = scalar(index);
    if (std::any_of(inputs.begin(), inputs.end(),
                    [](const auto &value) { return !value; }))
      return OBELISK_RT_INVALID_BYTECODE;
    std::optional<ByteSpan> trace =
        readByteSpan(image, frame, inputRegister(14));
    if (!trace || trace->size % sizeof(obelisk_rt_element_trace_slot_v1) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<obelisk_rt_element_trace_slot_v1> traceSlots(
        trace->size / sizeof(obelisk_rt_element_trace_slot_v1));
    if (!traceSlots.empty())
      std::memcpy(traceSlots.data(), trace->data, trace->size);
    obelisk_rt_object_v1 *watchOwner = nullptr;
    uint64_t ownerPayload = 0;
    uint32_t ownerManaged = 0;
    if (!readArgumentRef(inputRegister(0), watchOwner, ownerPayload,
                         ownerManaged))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    uint8_t *stateValue =
        context->stateValue.empty()
            ? nullptr
            : reinterpret_cast<uint8_t *>(context->stateValue.data());
    uint8_t *stateUnknown =
        context->stateUnknown.empty()
            ? nullptr
            : reinterpret_cast<uint8_t *>(context->stateUnknown.data());
    obelisk_rt_object_v1 *path = nullptr;
    obelisk_rt_status status =
        obelisk_rt_v1_reference_path_aggregate_element_create(
            lane, watchOwner, ownerPayload, ownerManaged, stateValue,
            stateUnknown, image.stateBitCount, static_cast<int64_t>(*inputs[0]),
            static_cast<int64_t>(*inputs[1]), static_cast<int64_t>(*inputs[2]),
            *inputs[3], *inputs[4], *inputs[5],
            static_cast<uint32_t>(*inputs[6]), *inputs[7],
            static_cast<uint32_t>(*inputs[8]),
            static_cast<uint32_t>(*inputs[9]), *inputs[10], *inputs[11],
            *inputs[12], traceSlots.data(), traceSlots.size(), &path);
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
    obelisk_rt_managed_word_v1 pathWord =
        obelisk_rt_managed_word_from_object(path);
    std::memcpy(frame.data + output.offset, &pathWord, sizeof(pathWord));
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
    obelisk_rt_managed_word_v1 word = 0;
    std::memcpy(&word, frame.data + input.offset + *bitOffset / 8,
                sizeof(word));
    obelisk_rt_object_v1 *object = obelisk_rt_object_from_managed_word(word);
    return writeManaged(outputRegister(0), object)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_CANDIDATE_ROOT: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    std::optional<uint64_t> bitOffset = scalar(1);
    std::optional<uint64_t> kindMask = scalar(2);
    if (!bitOffset || !kindMask || (*bitOffset & 63) != 0 ||
        *bitOffset > input.width || 64 > input.width - *bitOffset ||
        *kindMask == 0 ||
        (*kindMask & ~uint64_t{OBELISK_RT_MANAGED_ROOT_KIND_ALL}) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_managed_word_v1 word = 0;
    std::memcpy(&word, frame.data + input.offset + *bitOffset / 8,
                sizeof(word));
    word = obelisk_rt_v1_gc_candidate_root(context, word,
                                           static_cast<uint32_t>(*kindMask));
    obelisk_rt_object_v1 *object = obelisk_rt_object_from_managed_word(word);
    return writeManaged(outputRegister(0), object)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_WATCH: {
    std::optional<uint64_t> encodedKind = scalar(1);
    if (!encodedKind)
      return OBELISK_RT_INVALID_BYTECODE;
    auto kind = static_cast<obelisk_rt_managed_watch_kind>(*encodedKind);
    obelisk_rt_object_v1 *object = nullptr;
    uint64_t selector = 0;
    switch (kind) {
    case OBELISK_RT_MANAGED_WATCH_FIELD:
      if (!readManagedRef(inputRegister(0), object, selector))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_MANAGED_WATCH_CONTAINER_SIZE: {
      Layout input = layoutAt(image, frame.function, inputRegister(0));
      if (input.kind != OBELISK_RT_DBREG_MANAGED || input.size != 8)
        return OBELISK_RT_INVALID_BYTECODE;
      object = readManaged(inputRegister(0));
      break;
    }
    default:
      return OBELISK_RT_INVALID_BYTECODE;
    }
    uint64_t token = obelisk_rt_v1_managed_watch(object, kind, selector);
    return writeScalar(image, frame, outputRegister(0), token)
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
      if (*planeSize != sizeof(obelisk_rt_managed_word_v1))
        return OBELISK_RT_INVALID_BYTECODE;
      obelisk_rt_managed_word_v1 value = 0;
      obelisk_rt_status status =
          obelisk_rt_v1_object_read(object, offset, &value, sizeof(value));
      if (status != OBELISK_RT_OK)
        return status;
      obelisk_rt_object_v1 *decoded =
          obelisk_rt_object_from_managed_word(value);
      return value != obelisk_rt_managed_word_from_object(decoded) ||
                     !writeManaged(outputRegister(0), decoded)
                 ? OBELISK_RT_INVALID_HANDLE
                 : OBELISK_RT_OK;
    }
    if (output.kind == OBELISK_RT_DBREG_HANDLE) {
      if (*planeSize != sizeof(uint64_t) || output.size < 32)
        return OBELISK_RT_INVALID_BYTECODE;
      uint64_t stableID = UINT64_MAX;
      obelisk_rt_status status = obelisk_rt_v1_object_read(
          object, offset, &stableID, sizeof(stableID));
      if (status != OBELISK_RT_OK)
        return status;
      uint32_t kind = OBELISK_RT_DESCRIPTOR_EVENT;
      int64_t start = static_cast<int64_t>(stableID);
      int64_t begin = start == kInvalidHandleStart ? 0 : start;
      int64_t end = start == kInvalidHandleStart
                        ? 0
                        : (start == INT64_MAX ? start : start + 1);
      std::memset(frame.data + output.offset, 0, output.size);
      std::memcpy(frame.data + output.offset, &kind, sizeof(kind));
      std::memcpy(frame.data + output.offset + 8, &begin, sizeof(begin));
      std::memcpy(frame.data + output.offset + 16, &start, sizeof(start));
      std::memcpy(frame.data + output.offset + 24, &end, sizeof(end));
      return OBELISK_RT_OK;
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
      if (*planeSize != sizeof(obelisk_rt_managed_word_v1))
        return OBELISK_RT_INVALID_BYTECODE;
      obelisk_rt_managed_word_v1 value =
          obelisk_rt_managed_word_from_object(readManaged(inputRegister(1)));
      return obelisk_rt_v1_object_write(object, offset, &value, sizeof(value));
    }
    if (input.kind == OBELISK_RT_DBREG_HANDLE) {
      if (*planeSize != sizeof(uint64_t) || input.size < 32)
        return OBELISK_RT_INVALID_BYTECODE;
      uint32_t kind = 0;
      uint64_t stableID = UINT64_MAX;
      std::memcpy(&kind, frame.data + input.offset, sizeof(kind));
      std::memcpy(&stableID, frame.data + input.offset + 16, sizeof(stableID));
      if (kind != OBELISK_RT_DESCRIPTOR_EVENT)
        return OBELISK_RT_INVALID_BYTECODE;
      return obelisk_rt_v1_object_write(object, offset, &stableID,
                                        sizeof(stableID));
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
  case OBELISK_RT_INTRINSIC_V1_MANAGED_OVERRIDE: {
    obelisk_rt_object_v1 *object = nullptr;
    uint64_t offset = 0;
    auto planeSize = scalar(2);
    auto flags = scalar(3);
    auto owner = scalar(4);
    if (!readManagedRef(inputRegister(0), object, offset) || !planeSize ||
        !flags || !owner || *planeSize == 0 || (*flags & ~uint64_t{15}) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    bool fourState = (*flags & 8) != 0;
    if (fourState != (input.kind == OBELISK_RT_DBREG_LOGIC))
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t scratchPlaneSize = ((uint64_t{input.width} + 63) / 64) * 8;
    if (fourState &&
        (*planeSize > scratchPlaneSize || input.size != scratchPlaneSize * 2))
      return OBELISK_RT_INVALID_BYTECODE;
    if (!fourState && *planeSize > input.size)
      return OBELISK_RT_INVALID_BYTECODE;
    const uint8_t *value = frame.data + input.offset;
    const uint8_t *unknown = fourState ? value + scratchPlaneSize : nullptr;
    return obelisk_rt_v1_object_override(
        object, offset, *planeSize, fourState ? 1 : 0,
        (*flags & 1) != 0 ? 1 : 0, (*flags & 2) != 0 ? 1 : 0, *owner,
        (*flags & 4) != 0 ? 1 : 0, value, unknown);
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_RELEASE_OVERRIDE: {
    obelisk_rt_object_v1 *object = nullptr;
    uint64_t offset = 0;
    auto planeSize = scalar(1);
    auto flags = scalar(2);
    if (!readManagedRef(inputRegister(0), object, offset) || !planeSize ||
        !flags || *planeSize == 0 || (*flags & ~uint64_t{9}) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_object_release_override(object, offset, *planeSize,
                                                 (*flags & 8) != 0 ? 1 : 0,
                                                 (*flags & 1) != 0 ? 1 : 0);
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
      if (*planeSize != sizeof(obelisk_rt_managed_word_v1))
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
    uint32_t function =
        signature.flags & OBELISK_RT_INTRINSIC_SPAWN_FUNCTION_MASK;
    Function callee = functionAt(image, function);
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
    task.parent =
        (signature.flags & OBELISK_RT_INTRINSIC_SPAWN_DETACHED_CONTROLS) == 0
            ? context->activeLogicalProcessToken
            : 0;
    if (task.parent != 0)
      context->logicalProcessParentsWithChildren.insert(task.parent);
    task.programOwner = context->activeProgramOwner;
    if (!task.programOwner &&
        (signature.flags & OBELISK_RT_INTRINSIC_SPAWN_PROGRAM) != 0) {
      auto owner = scalar(callee.argumentCount);
      if (!owner || !*owner)
        return OBELISK_RT_INVALID_BYTECODE;
      task.programOwner = *owner;
    }
    obelisk_rt_random_split_unlocked(context, task.random);
    task.function = function;
    task.startupProcess =
        (signature.flags & OBELISK_RT_INTRINSIC_SPAWN_STARTUP) != 0;
    task.prioritySignal =
        (signature.flags & OBELISK_RT_INTRINSIC_SPAWN_PRIORITY_SIGNAL) != 0;
    task.urgent = task.startupProcess;
    task.scheduleRank = static_cast<uint32_t>(callee.initialScheduleRank);
    task.scratchOffset = scratchOffset;
    task.scratchSize = callee.scratchSize;
    task.frame = context->designTaskFrames.acquire(
        static_cast<size_t>(scratchOffset + callee.scratchSize));
    uint32_t copied = 0;
    std::unordered_map<uint32_t, uint64_t> retainedAutomaticStates;
    for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
      CaptureRecord capture = captureAt(image, index);
      if (capture.function != function)
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
      if (capture.unknownOffset != UINT64_MAX) {
        // Four-state registers use whole-limb planes, while the canonical
        // activation frame may use a smaller target-ABI plane.
        uint64_t sourcePlane = source.kind == OBELISK_RT_DBREG_LOGIC
                                   ? limbCount(source.width) * sizeof(uint64_t)
                                   : capture.planeSize;
        std::memcpy(task.frame.data() + capture.unknownOffset,
                    frame.data + source.offset + sourcePlane,
                    capture.planeSize);
      }
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
      // Process creation is the structural boundary that may make a rejected
      // slow-dominant scheduler shape profitable.
      obelisk_rt_invalidate_design_ready_cohort(context);
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
      if ((signature.flags & OBELISK_RT_INTRINSIC_SPAWN_DETACHED_CONTROLS) == 0)
        task.controls = context->activeControls;
      task.insertionSequence = context->nextProcessInsertionSequence++;
      task.observedEpoch = context->schedulerEpoch;
      for (const auto &[automaticID, count] : retainedAutomaticStates)
        context->nativeAutomaticStates.find(automaticID)
            ->second.referenceCount += count;
      OBELISK_RT_TRY {
        context->scheduledDesignTasks.push_back(std::move(task));
        uint64_t scheduledID = context->scheduledDesignTasks.back().id;
        OBELISK_RT_TRY {
          context->scheduledDesignTaskIndices[scheduledID] =
              context->scheduledDesignTasks.size() - 1;
          context->designPollCandidates.insert(scheduledID);
          if ((signature.flags &
               OBELISK_RT_INTRINSIC_SPAWN_DETACHED_CONTROLS) == 0)
            obelisk_rt_register_unstarted_actor(
                context, context->scheduledDesignTasks.back().phase,
                scheduledID);
          if (context->scheduledDesignTasks.back().programOwner)
            obelisk_rt_program_register_unlocked(
                context, scheduledID,
                context->scheduledDesignTasks.back().programOwner);
        }
        OBELISK_RT_CATCH_ALL {
          if (context->scheduledDesignTasks.back().programOwner)
            obelisk_rt_program_complete_unlocked(
                context, scheduledID,
                context->scheduledDesignTasks.back().programOwner);
          context->scheduledDesignTaskIndices.erase(scheduledID);
          context->designPollCandidates.erase(scheduledID);
          obelisk_rt_unregister_unstarted_actor(
              context, context->scheduledDesignTasks.back().phase, scheduledID);
          context->scheduledDesignTasks.pop_back();
          OBELISK_RT_RETHROW;
        }
        obelisk_rt_retain_controls_unlocked(
            context, context->scheduledDesignTasks.back().controls);
      }
      OBELISK_RT_CATCH_ALL {
        for (const auto &[automaticID, count] : retainedAutomaticStates)
          context->nativeAutomaticStates.find(automaticID)
              ->second.referenceCount -= count;
        OBELISK_RT_RETHROW;
      }
    }
    if ((signature.flags & OBELISK_RT_INTRINSIC_SPAWN_PRIME) != 0) {
      obelisk_rt_status status = obelisk_rt_prime_design_task(context, id);
      if (status != OBELISK_RT_OK)
        return status;
    }
    uint32_t destinationRegister = outputRegister(0);
    Layout destination = layoutAt(image, frame.function, destinationRegister);
    uint8_t *address = frame.data + destination.offset;
    std::memset(address, 0, destination.size);
    if (destination.kind == OBELISK_RT_DBREG_BITS && destination.width == 64 &&
        destination.size == sizeof(id)) {
      std::memcpy(address, &id, sizeof(id));
      return OBELISK_RT_OK;
    }
    if (destination.kind != OBELISK_RT_DBREG_HANDLE || destination.size < 32)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t kind = OBELISK_RT_DESCRIPTOR_PROCESS;
    int64_t begin = static_cast<int64_t>(id);
    int64_t end = id == uint64_t{INT64_MAX} ? begin : begin + 1;
    std::memcpy(address, &kind, 4);
    std::memcpy(address + 8, &begin, 8);
    std::memcpy(address + 16, &begin, 8);
    std::memcpy(address + 24, &end, 8);
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_INERTIAL_DRIVER: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    auto rise = scalar(2);
    auto fall = scalar(3);
    auto turnoff = scalar(4);
    auto codeUnit = scalar(5);
    auto component = scalar(6);
    auto flags = scalar(7);
    if (!rise || !fall || !turnoff || !codeUnit || !component || !flags ||
        *component > UINT32_MAX || *flags > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout valueLayout = layoutAt(image, frame.function, inputRegister(0));
    Logic value = readLogic(frame.data, valueLayout);
    auto suppress = [&] {
      return obelisk_rt_v1_scheduler_inertial_driver(
          context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
          reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
          context->execution->state_bit_count, UINT64_MAX, value.width,
          *codeUnit, static_cast<uint32_t>(*component),
          static_cast<uint32_t>(*flags), *rise, *fall, *turnoff, nullptr,
          nullptr);
    };
    Layout destination = layoutAt(image, frame.function, inputRegister(1));
    uint32_t kind = 0;
    uint64_t objectBase = 0;
    int64_t begin = 0, start = kInvalidHandleStart, end = 0;
    const uint8_t *address = frame.data + destination.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&objectBase, address + 8, 8);
    std::memcpy(&start, address + 16, 8);
    std::memcpy(&end, address + 24, 8);
    uint32_t staticID = 0;
    if (kind != OBELISK_RT_DESCRIPTOR_DRIVER ||
        !decodeStaticHandle(objectBase, staticID, begin) || begin > end)
      return OBELISK_RT_INVALID_HANDLE;
    if (start == kInvalidHandleStart)
      return suppress();
    int64_t first = start < begin ? begin - start : 0;
    int64_t last = static_cast<int64_t>(value.width);
    if (start > end || end - start < last)
      last = end - start;
    if (first >= last)
      return suppress();
    int64_t selectedStart = start + first;
    uint64_t stable = encodeStaticHandle(staticID, selectedStart);
    if (stable == UINT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t width = static_cast<uint64_t>(last - first);
    uint64_t bytes = (width + 7) / 8;
    std::vector<uint8_t> selectedValue(static_cast<size_t>(bytes), 0);
    std::vector<uint8_t> selectedUnknown(static_cast<size_t>(bytes), 0);
    for (uint64_t bitIndex = 0; bitIndex != width; ++bitIndex) {
      uint64_t source = static_cast<uint64_t>(first) + bitIndex;
      uint8_t mask = static_cast<uint8_t>(1u << (bitIndex % 8));
      if (bit(value.value, source))
        selectedValue[bitIndex / 8] |= mask;
      if (value.fourState && bit(value.unknown, source))
        selectedUnknown[bitIndex / 8] |= mask;
    }
    return obelisk_rt_v1_scheduler_inertial_driver(
        context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
        reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
        context->execution->state_bit_count, stable, width, *codeUnit,
        static_cast<uint32_t>(*component), static_cast<uint32_t>(*flags), *rise,
        *fall, *turnoff, selectedValue.data(), selectedUnknown.data());
  }
  case OBELISK_RT_INTRINSIC_V1_INERTIAL_PATH_DRIVER: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    auto rise = scalar(6);
    auto fall = scalar(7);
    auto turnoff = scalar(8);
    auto codeUnit = scalar(9);
    auto component = scalar(10);
    auto group = scalar(11);
    auto groupCount = scalar(12);
    auto flags = scalar(13);
    if (!rise || !fall || !turnoff || !codeUnit || !component || !group ||
        !groupCount || !flags || *component > UINT32_MAX ||
        *group > UINT32_MAX || *groupCount > UINT32_MAX || *flags > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout valueLayout = layoutAt(image, frame.function, inputRegister(0));
    Logic value = readLogic(frame.data, valueLayout);
    Layout destination = layoutAt(image, frame.function, inputRegister(1));
    uint32_t kind = 0;
    uint64_t objectBase = 0;
    int64_t begin = 0, start = kInvalidHandleStart, end = 0;
    const uint8_t *address = frame.data + destination.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&objectBase, address + 8, 8);
    std::memcpy(&start, address + 16, 8);
    std::memcpy(&end, address + 24, 8);
    uint32_t staticID = 0;
    if (kind != OBELISK_RT_DESCRIPTOR_DRIVER ||
        !decodeStaticHandle(objectBase, staticID, begin) || begin > end ||
        start == kInvalidHandleStart || start < begin || start > end ||
        end - start < static_cast<int64_t>(value.width))
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t stable = encodeStaticHandle(staticID, start);
    if (stable == UINT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;
    size_t bytes = static_cast<size_t>((value.width + 7) / 8);
    auto pack = [&](const Logic &logic) {
      std::vector<uint8_t> packed(bytes, 0);
      for (uint64_t bitIndex = 0; bitIndex != value.width; ++bitIndex)
        if (bit(logic.value, bitIndex))
          packed[static_cast<size_t>(bitIndex / 8)] |=
              static_cast<uint8_t>(1u << (bitIndex % 8));
      return packed;
    };
    std::vector<uint8_t> packedValue = pack(value);
    std::vector<uint8_t> packedUnknown(bytes, 0);
    if (value.fourState)
      for (uint64_t bitIndex = 0; bitIndex != value.width; ++bitIndex)
        if (bit(value.unknown, bitIndex))
          packedUnknown[static_cast<size_t>(bitIndex / 8)] |=
              static_cast<uint8_t>(1u << (bitIndex % 8));
    std::array<std::vector<uint8_t>, 4> masks;
    for (unsigned index = 0; index != masks.size(); ++index)
      masks[index] =
          pack(readLogic(frame.data, layoutAt(image, frame.function,
                                              inputRegister(index + 2))));
    if (site.inputCount == 14)
      return obelisk_rt_v1_scheduler_inertial_path_driver(
          context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
          reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
          context->execution->state_bit_count, stable, value.width, *codeUnit,
          static_cast<uint32_t>(*component), static_cast<uint32_t>(*group),
          static_cast<uint32_t>(*groupCount), static_cast<uint32_t>(*flags),
          *rise, *fall, *turnoff, packedValue.data(), packedUnknown.data(),
          masks[0].data(), masks[1].data(), masks[2].data(), masks[3].data());
    auto pulseReject = scalar(14);
    auto pulseError = scalar(15);
    if (!pulseReject || !pulseError)
      return OBELISK_RT_INVALID_BYTECODE;
    Logic pulseTransitions = readLogic(
        frame.data, layoutAt(image, frame.function, inputRegister(16)));
    bool exactTransitions =
        (*flags & OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS) != 0;
    if (!exactTransitions || pulseTransitions.width != value.width * 12)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint8_t> packedPulseTransitions;
    if (exactTransitions) {
      packedPulseTransitions.assign(
          static_cast<size_t>((pulseTransitions.width + 7) / 8), 0);
      for (uint64_t bitIndex = 0; bitIndex != pulseTransitions.width;
           ++bitIndex)
        if (bit(pulseTransitions.value, bitIndex))
          packedPulseTransitions[static_cast<size_t>(bitIndex / 8)] |=
              static_cast<uint8_t>(1u << (bitIndex % 8));
    }
    return obelisk_rt_v1_scheduler_inertial_path_driver_pulse(
        context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
        reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
        context->execution->state_bit_count, stable, value.width, *codeUnit,
        static_cast<uint32_t>(*component), static_cast<uint32_t>(*group),
        static_cast<uint32_t>(*groupCount), static_cast<uint32_t>(*flags),
        *rise, *fall, *turnoff, *pulseReject, *pulseError, packedValue.data(),
        packedUnknown.data(), masks[0].data(), masks[1].data(), masks[2].data(),
        masks[3].data(),
        exactTransitions ? packedPulseTransitions.data() : nullptr);
  }
  case OBELISK_RT_INTRINSIC_V1_INERTIAL_PATH_STORAGE: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    auto rise = scalar(7);
    auto fall = scalar(8);
    auto turnoff = scalar(9);
    auto siteID = scalar(10);
    auto component = scalar(11);
    auto group = scalar(12);
    auto groupCount = scalar(13);
    auto nonblocking = scalar(14);
    if (!rise || !fall || !turnoff || !siteID || !component || !group ||
        !groupCount || !nonblocking || *component > UINT32_MAX ||
        *group > UINT32_MAX || *groupCount > UINT32_MAX || *nonblocking > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout valueLayout = layoutAt(image, frame.function, inputRegister(0));
    Logic value = readLogic(frame.data, valueLayout);
    Layout destination = layoutAt(image, frame.function, inputRegister(1));
    uint32_t kind = 0;
    uint64_t objectBase = 0;
    int64_t begin = 0, start = kInvalidHandleStart, end = 0;
    const uint8_t *address = frame.data + destination.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&objectBase, address + 8, 8);
    std::memcpy(&start, address + 16, 8);
    std::memcpy(&end, address + 24, 8);
    uint32_t staticID = 0;
    if (kind != OBELISK_RT_DESCRIPTOR_STORAGE ||
        !decodeStaticHandle(objectBase, staticID, begin) || begin > end ||
        start == kInvalidHandleStart || start < begin || start > end ||
        end - start < static_cast<int64_t>(value.width))
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t stable = encodeStaticHandle(staticID, start);
    if (stable == UINT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;
    size_t bytes = static_cast<size_t>((value.width + 7) / 8);
    auto pack = [&](const Logic &logic) {
      std::vector<uint8_t> packed(bytes, 0);
      for (uint64_t bitIndex = 0; bitIndex != value.width; ++bitIndex)
        if (bit(logic.value, bitIndex))
          packed[static_cast<size_t>(bitIndex / 8)] |=
              static_cast<uint8_t>(1u << (bitIndex % 8));
      return packed;
    };
    std::vector<uint8_t> packedValue = pack(value);
    std::vector<uint8_t> packedUnknown(bytes, 0);
    if (value.fourState)
      for (uint64_t bitIndex = 0; bitIndex != value.width; ++bitIndex)
        if (bit(value.unknown, bitIndex))
          packedUnknown[static_cast<size_t>(bitIndex / 8)] |=
              static_cast<uint8_t>(1u << (bitIndex % 8));
    std::array<std::vector<uint8_t>, 5> masks;
    for (unsigned index = 0; index != masks.size(); ++index)
      masks[index] =
          pack(readLogic(frame.data, layoutAt(image, frame.function,
                                              inputRegister(index + 2))));
    if (site.inputCount == 15)
      return obelisk_rt_v1_scheduler_inertial_path_storage(
          context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
          reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
          context->execution->state_bit_count, stable, value.width, *siteID,
          static_cast<uint32_t>(*component), static_cast<uint32_t>(*group),
          static_cast<uint32_t>(*groupCount),
          static_cast<uint32_t>(*nonblocking), *rise, *fall, *turnoff,
          packedValue.data(), packedUnknown.data(), masks[0].data(),
          masks[1].data(), masks[2].data(), masks[3].data(), masks[4].data());
    auto pulseFlags = scalar(15);
    auto pulseReject = scalar(16);
    auto pulseError = scalar(17);
    if (!pulseFlags || !pulseReject || !pulseError || *pulseFlags > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    Logic pulseTransitions = readLogic(
        frame.data, layoutAt(image, frame.function, inputRegister(18)));
    bool exactTransitions =
        (*pulseFlags & OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS) != 0;
    if (!exactTransitions || pulseTransitions.width != value.width * 12)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<uint8_t> packedPulseTransitions;
    if (exactTransitions) {
      packedPulseTransitions.assign(
          static_cast<size_t>((pulseTransitions.width + 7) / 8), 0);
      for (uint64_t bitIndex = 0; bitIndex != pulseTransitions.width;
           ++bitIndex)
        if (bit(pulseTransitions.value, bitIndex))
          packedPulseTransitions[static_cast<size_t>(bitIndex / 8)] |=
              static_cast<uint8_t>(1u << (bitIndex % 8));
    }
    return obelisk_rt_v1_scheduler_inertial_path_storage_pulse(
        context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
        reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
        context->execution->state_bit_count, stable, value.width, *siteID,
        static_cast<uint32_t>(*component), static_cast<uint32_t>(*group),
        static_cast<uint32_t>(*groupCount), static_cast<uint32_t>(*nonblocking),
        static_cast<uint32_t>(*pulseFlags), *rise, *fall, *turnoff,
        *pulseReject, *pulseError, packedValue.data(), packedUnknown.data(),
        masks[0].data(), masks[1].data(), masks[2].data(), masks[3].data(),
        masks[4].data(),
        exactTransitions ? packedPulseTransitions.data() : nullptr);
  }
  case OBELISK_RT_INTRINSIC_V1_INERTIAL_DRIVER_STRENGTH_PAIR: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    auto rise = scalar(5);
    auto fall = scalar(6);
    auto turnoff = scalar(7);
    auto codeUnit = scalar(8);
    auto component = scalar(9);
    if (!rise || !fall || !turnoff || !codeUnit || !component ||
        *component > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    Logic low = readLogic(frame.data,
                          layoutAt(image, frame.function, inputRegister(0)));
    Logic high = readLogic(frame.data,
                           layoutAt(image, frame.function, inputRegister(2)));
    Logic transition = readLogic(
        frame.data, layoutAt(image, frame.function, inputRegister(4)));
    struct Selection {
      uint64_t stable = UINT64_MAX;
      int64_t first = 0;
      int64_t last = 0;
      bool suppressed = false;
    };
    auto select = [&](unsigned inputIndex) -> std::optional<Selection> {
      Layout destination =
          layoutAt(image, frame.function, inputRegister(inputIndex));
      uint32_t kind = 0;
      uint64_t objectBase = 0;
      int64_t begin = 0, start = kInvalidHandleStart, end = 0;
      const uint8_t *address = frame.data + destination.offset;
      std::memcpy(&kind, address, 4);
      std::memcpy(&objectBase, address + 8, 8);
      std::memcpy(&start, address + 16, 8);
      std::memcpy(&end, address + 24, 8);
      uint32_t staticID = 0;
      if (kind != OBELISK_RT_DESCRIPTOR_DRIVER ||
          !decodeStaticHandle(objectBase, staticID, begin) || begin > end)
        return std::nullopt;
      if (start == kInvalidHandleStart)
        return Selection{UINT64_MAX, 0, 0, true};
      int64_t first = start < begin ? begin - start : 0;
      int64_t last = static_cast<int64_t>(low.width);
      if (start > end || end - start < last)
        last = end - start;
      if (first >= last)
        return Selection{UINT64_MAX, first, last, true};
      uint64_t stable = encodeStaticHandle(staticID, start + first);
      if (stable == UINT64_MAX)
        return std::nullopt;
      return Selection{stable, first, last, false};
    };
    std::optional<Selection> lowSelection = select(1);
    std::optional<Selection> highSelection = select(3);
    if (!lowSelection || !highSelection)
      return OBELISK_RT_INVALID_HANDLE;
    auto suppress = [&] {
      return obelisk_rt_v1_scheduler_inertial_driver_strength_pair(
          context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
          reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
          context->execution->state_bit_count, UINT64_MAX, UINT64_MAX,
          low.width, *codeUnit, static_cast<uint32_t>(*component), *rise, *fall,
          *turnoff, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    };
    if (lowSelection->suppressed || highSelection->suppressed) {
      if (lowSelection->suppressed != highSelection->suppressed ||
          lowSelection->first != highSelection->first ||
          lowSelection->last != highSelection->last)
        return OBELISK_RT_INVALID_HANDLE;
      return suppress();
    }
    if (lowSelection->first != highSelection->first ||
        lowSelection->last != highSelection->last)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t width =
        static_cast<uint64_t>(lowSelection->last - lowSelection->first);
    uint64_t bytes = (width + 7) / 8;
    struct Planes {
      std::vector<uint8_t> value;
      std::vector<uint8_t> unknown;
    };
    auto extract = [&](const Logic &source) {
      Planes planes{std::vector<uint8_t>(static_cast<size_t>(bytes), 0),
                    std::vector<uint8_t>(static_cast<size_t>(bytes), 0)};
      for (uint64_t bitIndex = 0; bitIndex != width; ++bitIndex) {
        uint64_t sourceBit =
            static_cast<uint64_t>(lowSelection->first) + bitIndex;
        uint8_t mask = static_cast<uint8_t>(1u << (bitIndex % 8));
        if (bit(source.value, sourceBit))
          planes.value[bitIndex / 8] |= mask;
        if (source.fourState && bit(source.unknown, sourceBit))
          planes.unknown[bitIndex / 8] |= mask;
      }
      return planes;
    };
    Planes lowPlanes = extract(low);
    Planes highPlanes = extract(high);
    Planes transitionPlanes = extract(transition);
    return obelisk_rt_v1_scheduler_inertial_driver_strength_pair(
        context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
        reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
        context->execution->state_bit_count, lowSelection->stable,
        highSelection->stable, width, *codeUnit,
        static_cast<uint32_t>(*component), *rise, *fall, *turnoff,
        lowPlanes.value.data(), lowPlanes.unknown.data(),
        highPlanes.value.data(), highPlanes.unknown.data(),
        transitionPlanes.value.data(), transitionPlanes.unknown.data());
  }
  case OBELISK_RT_INTRINSIC_V1_INERTIAL_PATH_STRENGTH_PAIR: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    auto rise = scalar(9);
    auto fall = scalar(10);
    auto turnoff = scalar(11);
    auto codeUnit = scalar(12);
    auto component = scalar(13);
    auto group = scalar(14);
    auto groupCount = scalar(15);
    if (!rise || !fall || !turnoff || !codeUnit || !component || !group ||
        !groupCount || *component > UINT32_MAX || *group > UINT32_MAX ||
        *groupCount > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    Logic low = readLogic(frame.data,
                          layoutAt(image, frame.function, inputRegister(0)));
    Logic high = readLogic(frame.data,
                           layoutAt(image, frame.function, inputRegister(2)));
    Logic transition = readLogic(
        frame.data, layoutAt(image, frame.function, inputRegister(4)));
    struct Selection {
      uint64_t stable = UINT64_MAX;
      int64_t first = 0;
      int64_t last = 0;
    };
    auto select = [&](unsigned inputIndex) -> std::optional<Selection> {
      Layout destination =
          layoutAt(image, frame.function, inputRegister(inputIndex));
      uint32_t kind = 0;
      uint64_t objectBase = 0;
      int64_t begin = 0, start = kInvalidHandleStart, end = 0;
      const uint8_t *address = frame.data + destination.offset;
      std::memcpy(&kind, address, 4);
      std::memcpy(&objectBase, address + 8, 8);
      std::memcpy(&start, address + 16, 8);
      std::memcpy(&end, address + 24, 8);
      uint32_t staticID = 0;
      if (kind != OBELISK_RT_DESCRIPTOR_DRIVER ||
          !decodeStaticHandle(objectBase, staticID, begin) || begin > end ||
          start == kInvalidHandleStart)
        return std::nullopt;
      int64_t first = start < begin ? begin - start : 0;
      int64_t last = static_cast<int64_t>(low.width);
      if (start > end || end - start < last)
        last = end - start;
      if (first >= last)
        return std::nullopt;
      uint64_t stable = encodeStaticHandle(staticID, start + first);
      if (stable == UINT64_MAX)
        return std::nullopt;
      return Selection{stable, first, last};
    };
    std::optional<Selection> lowSelection = select(1);
    std::optional<Selection> highSelection = select(3);
    if (!lowSelection || !highSelection ||
        lowSelection->first != highSelection->first ||
        lowSelection->last != highSelection->last)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t width =
        static_cast<uint64_t>(lowSelection->last - lowSelection->first);
    uint64_t bytes = (width + 7) / 8;
    struct Planes {
      std::vector<uint8_t> value;
      std::vector<uint8_t> unknown;
    };
    auto extract = [&](const Logic &source) {
      Planes planes{std::vector<uint8_t>(static_cast<size_t>(bytes), 0),
                    std::vector<uint8_t>(static_cast<size_t>(bytes), 0)};
      for (uint64_t bitIndex = 0; bitIndex != width; ++bitIndex) {
        uint64_t sourceBit =
            static_cast<uint64_t>(lowSelection->first) + bitIndex;
        uint8_t mask = static_cast<uint8_t>(1u << (bitIndex % 8));
        if (bit(source.value, sourceBit))
          planes.value[bitIndex / 8] |= mask;
        if (source.fourState && bit(source.unknown, sourceBit))
          planes.unknown[bitIndex / 8] |= mask;
      }
      return planes;
    };
    auto extractMask = [&](unsigned inputIndex) {
      Logic source = readLogic(frame.data, layoutAt(image, frame.function,
                                                    inputRegister(inputIndex)));
      return extract(source).value;
    };
    Planes lowPlanes = extract(low);
    Planes highPlanes = extract(high);
    Planes transitionPlanes = extract(transition);
    std::array<std::vector<uint8_t>, 4> masks;
    for (unsigned index = 0; index != masks.size(); ++index)
      masks[index] = extractMask(index + 5);
    if (site.inputCount == 20) {
      auto pulseFlags = scalar(16);
      auto pulseReject = scalar(17);
      auto pulseError = scalar(18);
      if (!pulseFlags || !pulseReject || !pulseError ||
          *pulseFlags > UINT32_MAX)
        return OBELISK_RT_INVALID_BYTECODE;
      Logic sourceTransitions = readLogic(
          frame.data, layoutAt(image, frame.function, inputRegister(19)));
      uint64_t packedWidth = width * 12;
      std::vector<uint8_t> pulseTransitions(
          static_cast<size_t>((packedWidth + 7) / 8), 0);
      // Each 30.5.1 transition class is an independent packed mask. A clipped
      // driver view must select the same bit window from every class rather
      // than one contiguous window from the first class.
      for (uint64_t transitionIndex = 0; transitionIndex != 12;
           ++transitionIndex)
        for (uint64_t bitIndex = 0; bitIndex != width; ++bitIndex) {
          uint64_t sourceBit = transitionIndex * low.width +
                               static_cast<uint64_t>(lowSelection->first) +
                               bitIndex;
          if (bit(sourceTransitions.value, sourceBit))
            pulseTransitions[(transitionIndex * width + bitIndex) / 8] |=
                static_cast<uint8_t>(
                    1u << ((transitionIndex * width + bitIndex) % 8));
        }
      return obelisk_rt_v1_scheduler_inertial_path_strength_pair_pulse(
          context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
          reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
          context->execution->state_bit_count, lowSelection->stable,
          highSelection->stable, width, *codeUnit,
          static_cast<uint32_t>(*component), static_cast<uint32_t>(*group),
          static_cast<uint32_t>(*groupCount),
          static_cast<uint32_t>(*pulseFlags), *rise, *fall, *turnoff,
          *pulseReject, *pulseError, lowPlanes.value.data(),
          lowPlanes.unknown.data(), highPlanes.value.data(),
          highPlanes.unknown.data(), transitionPlanes.value.data(),
          transitionPlanes.unknown.data(), masks[0].data(), masks[1].data(),
          masks[2].data(), masks[3].data(), pulseTransitions.data());
    }
    return obelisk_rt_v1_scheduler_inertial_path_strength_pair(
        context, reinterpret_cast<uint8_t *>(context->stateValue.data()),
        reinterpret_cast<uint8_t *>(context->stateUnknown.data()),
        context->execution->state_bit_count, lowSelection->stable,
        highSelection->stable, width, *codeUnit,
        static_cast<uint32_t>(*component), static_cast<uint32_t>(*group),
        static_cast<uint32_t>(*groupCount), *rise, *fall, *turnoff,
        lowPlanes.value.data(), lowPlanes.unknown.data(),
        highPlanes.value.data(), highPlanes.unknown.data(),
        transitionPlanes.value.data(), transitionPlanes.unknown.data(),
        masks[0].data(), masks[1].data(), masks[2].data(), masks[3].data());
  }
  case OBELISK_RT_INTRINSIC_V1_NBA:
  case OBELISK_RT_INTRINSIC_V1_STATIC_NBA:
  case OBELISK_RT_INTRINSIC_V1_CLOCKING_NBA: {
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
    bool driver = descriptorKind == OBELISK_RT_DESCRIPTOR_DRIVER;
    if ((descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE && !driver) ||
        begin > end)
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
    bool clocking = signature.id == OBELISK_RT_INTRINSIC_V1_CLOCKING_NBA;
    uint64_t staticSiteID = UINT64_MAX;
    uint64_t clockingOutput = UINT64_MAX;
    uint32_t delayInputCount = staticSite ? 2
                               : clocking ? site.inputCount - 1
                                          : site.inputCount;
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
    if (clocking) {
      auto encodedOutput = scalar(site.inputCount - 1);
      if (!encodedOutput || *encodedOutput == UINT64_MAX)
        return OBELISK_RT_INVALID_BYTECODE;
      clockingOutput = *encodedOutput;
    }
    if (managedValue || automatic || boundedStatic) {
      if (driver && (managedValue || automatic || !boundedStatic))
        return OBELISK_RT_INVALID_HANDLE;
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
      if (!driver && staticSiteID != UINT64_MAX && !stringValue &&
          !managedValue && boundedStatic && selectedWidth <= 64 &&
          context->nativeSchedulePlan && !context->nativeScheduleDeoptimized) {
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
      update.clockingOutput = clockingOutput;
      update.stringValue = stringValue;
      update.managedValue = managedValue;
      update.driver = driver;
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
      if (!driver && staticSiteID != UINT64_MAX && !stringValue &&
          !managedValue && boundedStatic && context->nativeSchedulePlan &&
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
    if (clocking)
      return OBELISK_RT_INVALID_HANDLE;
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
    if (kind != OBELISK_RT_DESCRIPTOR_EVENT)
      return OBELISK_RT_INVALID_HANDLE;
    // The canonical event-null bytecode handle carries an all-ones start.
    // IEEE 1800-2017 15.5.5.2 makes every trigger of it a no-op, including
    // delayed and nonblocking triggers.
    if (start == -1)
      return OBELISK_RT_OK;
    if (start < 0)
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
    if (!obelisk_rt_notify_event_order_waiters_unlocked(context, stableID))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_DESIGN
                 : context->schedulerStatus;
    if (!obelisk_rt_notify_observer_event_unlocked(context, stableID))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_DESIGN
                 : context->schedulerStatus;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_EVENT_REPLACE_AFTER: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout event = layoutAt(image, frame.function, inputRegister(0));
    uint32_t kind = 0;
    int64_t start = -1;
    const uint8_t *address = frame.data + event.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&start, address + 16, 8);
    if (kind != OBELISK_RT_DESCRIPTOR_EVENT)
      return OBELISK_RT_INVALID_HANDLE;
    // IEEE 1800-2017 15.5.5.2 makes a null named event inert for both
    // scheduling and cancellation.
    if (start == -1)
      return OBELISK_RT_OK;
    if (start < 0)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t delay = 0;
    if (site.inputCount == 2) {
      std::optional<uint64_t> encodedDelay = scalar(1);
      if (!encodedDelay)
        return OBELISK_RT_INVALID_BYTECODE;
      delay = *encodedDelay;
    }
    obelisk_rt_v1_scheduler_event_replace_after(
        context, static_cast<uint64_t>(start), site.inputCount == 2 ? 1u : 0u,
        delay);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    return context->schedulerStatus;
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
  case OBELISK_RT_INTRINSIC_V1_WAIT_ORDER_FAILED:
    return sentinel(0, obelisk_rt_v1_scheduler_wait_order_failed(context));
  case OBELISK_RT_INTRINSIC_V1_CLOCK_OCCURRENCE_CONSUME:
    return sentinel(
        0, obelisk_rt_v1_clock_occurrence_consume(context, signature.flags));
  case OBELISK_RT_INTRINSIC_V1_NOCHANGE_UPDATE: {
    std::optional<uint64_t> mask = readScalar(image, frame, inputRegister(0));
    std::optional<uint64_t> start = readScalar(image, frame, inputRegister(1));
    std::optional<uint64_t> end = readScalar(image, frame, inputRegister(2));
    if (!mask || !start || !end)
      return OBELISK_RT_INVALID_DESIGN;
    return sentinel(
        0, obelisk_rt_v1_nochange_update(context, signature.flags, *mask,
                                         static_cast<int64_t>(*start),
                                         static_cast<int64_t>(*end)));
  }
  case OBELISK_RT_INTRINSIC_V1_STATE_ALLOC:
  case OBELISK_RT_INTRINSIC_V1_STATE_ALLOC_TYPED: {
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
      if (signature.id == OBELISK_RT_INTRINSIC_V1_STATE_ALLOC_TYPED) {
        if (site.inputCount < 4 || ((site.inputCount - 1) % 3) != 0)
          return OBELISK_RT_INVALID_BYTECODE;
        std::vector<obelisk_rt_managed_root_slot_v1> slots;
        slots.reserve((site.inputCount - 1) / 3);
        for (uint32_t index = 1; index != site.inputCount; index += 3) {
          std::optional<uint64_t> bitOffset = scalar(index);
          std::optional<uint64_t> kindMask = scalar(index + 1);
          std::optional<uint64_t> flags = scalar(index + 2);
          if (!bitOffset || !kindMask || !flags || *kindMask > UINT32_MAX ||
              *flags > UINT32_MAX)
            return OBELISK_RT_INVALID_BYTECODE;
          slots.push_back({*bitOffset, static_cast<uint32_t>(*kindMask),
                           static_cast<uint32_t>(*flags)});
        }
        status = obelisk_rt_v1_native_state_alloc_with_typed_roots(
            context, initial.width, value, unknown, slots.data(), slots.size(),
            &stable);
      } else if (site.inputCount > 1) {
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
  case OBELISK_RT_INTRINSIC_V1_CONTROL_BOUNDARY: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> activation = scalar(0);
    if (!activation || signature.flags == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_control_boundary(context, *activation,
                                          signature.flags);
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
  case OBELISK_RT_INTRINSIC_V1_CONTROL_ESCAPE_PENDING:
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    return sentinel(0, obelisk_rt_v1_control_escape_pending(context));
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
  case OBELISK_RT_INTRINSIC_V1_DEFERRED_ENQUEUE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> siteID = scalar(0);
    if (!siteID || *siteID == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    if (site.inputCount == 2) {
      std::optional<uint64_t> assertionID = scalar(1);
      if (!assertionID || *assertionID == 0)
        return OBELISK_RT_INVALID_BYTECODE;
      return sentinel(0, obelisk_rt_v1_deferred_enqueue_for_assertion(
                             context, *siteID, *assertionID));
    }
    return sentinel(0, obelisk_rt_v1_deferred_enqueue(context, *siteID));
  }
  case OBELISK_RT_INTRINSIC_V1_DEFERRED_MATURE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> ticket = scalar(0);
    if (!ticket || *ticket == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(0, obelisk_rt_v1_deferred_mature(context, *ticket));
  }
  case OBELISK_RT_INTRINSIC_V1_ASSERTION_CONTROL: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint32_t action = signature.flags;
    size_t assertionIDIndex = 0;
    if (action == 0) {
      std::optional<uint64_t> dynamicAction = scalar(0);
      if (!dynamicAction || *dynamicAction > UINT32_MAX)
        return OBELISK_RT_INVALID_BYTECODE;
      action = static_cast<uint32_t>(*dynamicAction);
      assertionIDIndex = 1;
    }
    std::optional<uint64_t> assertionID = scalar(assertionIDIndex);
    if (!assertionID || *assertionID == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_assertion_control(context, action, *assertionID);
  }
  case OBELISK_RT_INTRINSIC_V1_ASSERTION_ENABLED: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> assertionID = scalar(0);
    if (!assertionID || *assertionID == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(0, obelisk_rt_v1_assertion_enabled(context, *assertionID));
  }
  case OBELISK_RT_INTRINSIC_V1_ASSERTION_ACTION_STATE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> assertionID = scalar(0);
    if (!assertionID || *assertionID == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(
        0, obelisk_rt_v1_assertion_action_state(context, *assertionID));
  }
  case OBELISK_RT_INTRINSIC_V1_ASSERTION_KILL_EPOCH: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> assertionID = scalar(0);
    if (!assertionID || *assertionID == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(0,
                    obelisk_rt_v1_assertion_kill_epoch(context, *assertionID));
  }
  case OBELISK_RT_INTRINSIC_V1_MONITOR_REGISTER: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout process = layoutAt(image, frame.function, inputRegister(0));
    if (process.kind == OBELISK_RT_DBREG_BITS && process.width == 64 &&
        process.size == sizeof(uint64_t)) {
      uint64_t logicalProcess = 0;
      std::memcpy(&logicalProcess, frame.data + process.offset,
                  sizeof(logicalProcess));
      return obelisk_rt_v1_monitor_register_logical(context, logicalProcess);
    }
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
  case OBELISK_RT_INTRINSIC_V1_PROCESS_CURRENT:
    return sentinel(0, obelisk_rt_v1_process_current(context));
  case OBELISK_RT_INTRINSIC_V1_PROCESS_STATUS: {
    auto logicalProcess = scalar(0);
    obelisk_rt_process_state state = OBELISK_RT_PROCESS_FINISHED;
    if (!logicalProcess)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_process_status(context, *logicalProcess, &state);
    return status == OBELISK_RT_OK ? sentinel(0, state) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_PROCESS_RANDOM_GET: {
    auto logicalProcess = scalar(0);
    obelisk_rt_random_state_v1 state{};
    if (!logicalProcess)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_process_random_get(context, *logicalProcess, &state);
    if (status != OBELISK_RT_OK)
      return status;
    status = sentinel(0, state.state);
    return status == OBELISK_RT_OK ? sentinel(1, state.increment) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_PROCESS_RANDOM_SET: {
    auto logicalProcess = scalar(0);
    auto state = scalar(1);
    auto increment = scalar(2);
    if (!logicalProcess || !state || !increment)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_random_state_v1 snapshot{*state, *increment};
    return obelisk_rt_v1_process_random_set(context, *logicalProcess,
                                            &snapshot);
  }
  case OBELISK_RT_INTRINSIC_V1_DUMP_OPEN: {
    auto path = bytes(0);
    if (!path)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_dump_open(context, path->data, path->size);
  }
  case OBELISK_RT_INTRINSIC_V1_DUMP_OPEN_STRING: {
    obelisk_rt_string_v1 path = 0;
    if (!readString(inputRegister(0), path))
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_dump_open_string(context, path);
  }
  case OBELISK_RT_INTRINSIC_V1_DUMP_TIMESCALE: {
    auto exponent = scalar(0);
    if (!exponent)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_dump_timescale(
        context, static_cast<int32_t>(static_cast<uint32_t>(*exponent)));
  }
  case OBELISK_RT_INTRINSIC_V1_DUMP_VARS: {
    auto levels = scalar(0);
    auto scope = bytes(1);
    if (!levels || !scope)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_dump_vars(context, *levels, scope->data, scope->size);
  }
  case OBELISK_RT_INTRINSIC_V1_DUMP_ALL:
    return obelisk_rt_v1_dump_all(context);
  case OBELISK_RT_INTRINSIC_V1_DUMP_LIMIT: {
    auto limit = scalar(0);
    if (!limit)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_dump_limit(context, *limit);
  }
  case OBELISK_RT_INTRINSIC_V1_DUMP_FLUSH:
    return obelisk_rt_v1_dump_flush(context);
  case OBELISK_RT_INTRINSIC_V1_DUMP_CONTROL:
    return obelisk_rt_v1_dump_control(context, signature.flags);
  case OBELISK_RT_INTRINSIC_V1_DUMP_PORTS: {
    obelisk_rt_string_v1 path = 0;
    obelisk_rt_string_v1 scope = 0;
    auto exponent = scalar(2);
    if (!readString(inputRegister(0), path) ||
        !readString(inputRegister(1), scope) || !exponent)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_dump_ports(
        context, path, scope,
        static_cast<int32_t>(static_cast<uint32_t>(*exponent)));
  }
  case OBELISK_RT_INTRINSIC_V1_DUMP_PORTS_CONTROL: {
    obelisk_rt_string_v1 path = 0;
    auto value = scalar(1);
    if (!readString(inputRegister(0), path) || !value)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_dump_ports_control(context, path, signature.flags,
                                            *value);
  }
  case OBELISK_RT_INTRINSIC_V1_MONITOR_CONTROL:
    return obelisk_rt_v1_monitor_control(context, signature.flags);
  case OBELISK_RT_INTRINSIC_V1_MONITOR_CURRENT:
    return sentinel(0, obelisk_rt_v1_monitor_current(context));
  case OBELISK_RT_INTRINSIC_V1_IMPORT:
  case OBELISK_RT_INTRINSIC_V1_DPI_IMPORT: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    if (signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT &&
        (!designBytecodeDpiOpenAggregatePack ||
         !designBytecodeDpiOpenAggregateUnpack ||
         !designBytecodeDpiOpenPrepareRecursive ||
         !designBytecodeDpiOpenFinishRecursive ||
         !designBytecodeDpiAggregatePack || !designBytecodeDpiAggregateUnpack ||
         !designBytecodeDpiOpenAggregateRootsPush ||
         !designBytecodeDpiAggregateRootsPop))
      return OBELISK_RT_INVALID_BYTECODE;
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
    struct DPIABIEntry {
      uint32_t kind = 0;
      uint32_t direction = 0;
      uint32_t width = 0;
      uint32_t flags = 0;
    };
    std::vector<DPIABIEntry> dpiEntries;
    struct DPIOpenLayout {
      uint32_t storage = 0;
      uint32_t elementKind = 0;
      uint32_t elementWidth = 0;
      uint32_t fourState = 0;
      uint32_t transportFourState = 0;
      uint64_t transportWidth = 0;
      uint64_t elementCSize = 0;
      uint64_t elementStringCount = 0;
      uint32_t elementCAlignment = 0;
      int64_t packedLeft = 0;
      int64_t packedRight = 0;
      std::vector<int64_t> elementPlan;
      std::vector<int64_t> ranges;
      std::vector<int64_t> sourceRanges;
      std::vector<int64_t> shapePlan;
    };
    struct DPIOpenStorage {
      std::vector<uint8_t> data;
      std::vector<uint8_t> flatValue;
      std::vector<uint8_t> flatUnknown;
      std::vector<obelisk_rt_dpi_dimension_v1> dimensions;
      obelisk_rt_dpi_open_array_v1 descriptor{};
      std::unique_ptr<void, decltype(&std::free)> recursiveAllocation{
          nullptr, &std::free};
      obelisk_rt_object_v1 *container = nullptr;
      uint64_t elementCount = 0;
      uint64_t elementSize = 0;
      uint64_t dataSize = 0;
      uint64_t planeSize = 0;
      uint64_t totalWidth = 0;
      bool dynamic = false;
    };
    struct DPIAggregateLayout {
      uint64_t cSize = 0;
      uint64_t stringCount = 0;
      uint32_t cAlignment = 0;
      std::vector<int64_t> plan;
    };
    struct DPIAggregateStorage {
      std::vector<uint8_t> data;
    };
    std::vector<std::optional<DPIOpenLayout>> dpiOpenLayouts;
    std::vector<std::optional<DPIAggregateLayout>> dpiAggregateLayouts;
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
      uint64_t layoutOffset = 56 + entryCount * 16;
      if (layoutOffset > metadata->size)
        return OBELISK_RT_INVALID_BYTECODE;
      dpiOpenLayouts.resize(static_cast<size_t>(entryCount));
      for (uint64_t index = 0; index != entryCount; ++index) {
        if (layoutOffset > metadata->size - 4)
          return OBELISK_RT_INVALID_BYTECODE;
        uint32_t recordSize = read32(metadata->data + layoutOffset);
        if (recordSize == 0) {
          layoutOffset += 4;
          continue;
        }
        if (recordSize < 80 || recordSize > metadata->size - layoutOffset)
          return OBELISK_RT_INVALID_BYTECODE;
        const uint8_t *record = metadata->data + layoutOffset;
        DPIOpenLayout layout;
        layout.storage = read32(record + 4);
        layout.elementKind = read32(record + 8);
        layout.elementWidth = read32(record + 12);
        layout.fourState = read32(record + 16);
        layout.transportWidth = read64(record + 20);
        layout.packedLeft = static_cast<int64_t>(read64(record + 28));
        layout.packedRight = static_cast<int64_t>(read64(record + 36));
        uint32_t dimensions = read32(record + 44);
        layout.elementCAlignment = read32(record + 48);
        layout.transportFourState = read32(record + 52);
        layout.elementCSize = read64(record + 56);
        layout.elementStringCount = read64(record + 64);
        uint64_t planWords = read64(record + 72);
        uint64_t shapeWords =
            layout.storage == 2 ? uint64_t{dimensions} * 8 : uint64_t{0};
        if (planWords == 0 || planWords % 8 != 0 ||
            planWords > (UINT32_MAX - 80) / 8 ||
            uint64_t{dimensions} > (UINT32_MAX - 80 - planWords * 8) / 32 ||
            shapeWords >
                (UINT32_MAX - 80 - planWords * 8 - uint64_t{dimensions} * 32) /
                    8 ||
            recordSize != 80 + planWords * 8 + uint64_t{dimensions} * 32 +
                              shapeWords * 8 ||
            layout.storage > 2 || layout.elementWidth == 0 ||
            layout.fourState > 1 || layout.transportFourState > 1 ||
            (layout.storage == 1 && layout.transportFourState != 0) ||
            layout.transportWidth == 0 || layout.elementCSize == 0 ||
            layout.elementCAlignment == 0 ||
            (layout.elementCAlignment & (layout.elementCAlignment - 1)) != 0 ||
            layout.elementStringCount > (UINT64_MAX - layout.elementCSize) / 8)
          return OBELISK_RT_INVALID_BYTECODE;
        layout.elementPlan.reserve(static_cast<size_t>(planWords));
        for (uint64_t word = 0; word != planWords; ++word)
          layout.elementPlan.push_back(static_cast<int64_t>(
              read64(record + 80 + word * sizeof(uint64_t))));
        uint64_t rangesOffset = 80 + planWords * 8;
        layout.ranges.reserve(uint64_t{dimensions} * 2);
        for (uint32_t dimension = 0; dimension != dimensions; ++dimension) {
          layout.ranges.push_back(static_cast<int64_t>(
              read64(record + rangesOffset + uint64_t{dimension} * 16)));
          layout.ranges.push_back(static_cast<int64_t>(
              read64(record + rangesOffset + 8 + uint64_t{dimension} * 16)));
        }
        uint64_t sourceRangesOffset = rangesOffset + uint64_t{dimensions} * 16;
        layout.sourceRanges.reserve(uint64_t{dimensions} * 2);
        for (uint32_t dimension = 0; dimension != dimensions; ++dimension) {
          layout.sourceRanges.push_back(static_cast<int64_t>(
              read64(record + sourceRangesOffset + uint64_t{dimension} * 16)));
          layout.sourceRanges.push_back(static_cast<int64_t>(read64(
              record + sourceRangesOffset + 8 + uint64_t{dimension} * 16)));
        }
        uint64_t shapeOffset = sourceRangesOffset + uint64_t{dimensions} * 16;
        layout.shapePlan.reserve(static_cast<size_t>(shapeWords));
        for (uint64_t word = 0; word != shapeWords; ++word)
          layout.shapePlan.push_back(static_cast<int64_t>(
              read64(record + shapeOffset + word * sizeof(uint64_t))));
        dpiOpenLayouts[index] = std::move(layout);
        layoutOffset += recordSize;
      }
      dpiAggregateLayouts.resize(static_cast<size_t>(entryCount));
      for (uint64_t index = 0; index != entryCount; ++index) {
        if (layoutOffset > metadata->size - 4)
          return OBELISK_RT_INVALID_BYTECODE;
        uint32_t recordSize = read32(metadata->data + layoutOffset);
        if (recordSize == 0) {
          layoutOffset += 4;
          continue;
        }
        if (recordSize < 32 || recordSize > metadata->size - layoutOffset)
          return OBELISK_RT_INVALID_BYTECODE;
        const uint8_t *record = metadata->data + layoutOffset;
        DPIAggregateLayout layout;
        layout.cAlignment = read32(record + 4);
        layout.cSize = read64(record + 8);
        layout.stringCount = read64(record + 16);
        uint64_t planWords = read64(record + 24);
        if (layout.cAlignment == 0 ||
            (layout.cAlignment & (layout.cAlignment - 1)) != 0 ||
            layout.cSize == 0 || planWords == 0 || planWords % 8 != 0 ||
            planWords > (UINT32_MAX - 32) / 8 ||
            recordSize != 32 + planWords * 8 ||
            layout.stringCount > (UINT64_MAX - layout.cSize) / 8)
          return OBELISK_RT_INVALID_BYTECODE;
        layout.plan.reserve(static_cast<size_t>(planWords));
        for (uint64_t word = 0; word != planWords; ++word)
          layout.plan.push_back(static_cast<int64_t>(
              read64(record + 32 + word * sizeof(uint64_t))));
        dpiAggregateLayouts[index] = std::move(layout);
        layoutOffset += recordSize;
      }
      uint64_t sourceOffset = layoutOffset;
      if (sourceOffset > metadata->size ||
          importSite.source_file_size != metadata->size - sourceOffset ||
          uint64_t{site.inputCount} != uint64_t{logicalInputs} + 1 ||
          uint64_t{site.outputCount} != uint64_t{logicalOutputs} + 1)
        return OBELISK_RT_INVALID_BYTECODE;
      importSite.source_file =
          importSite.source_file_size
              ? reinterpret_cast<const char *>(metadata->data + sourceOffset)
              : nullptr;

      auto entry = [&](uint64_t index) {
        const uint8_t *data = metadata->data + 56 + index * 16;
        return DPIABIEntry{read32(data), read32(data + 4), read32(data + 8),
                           read32(data + 12)};
      };
      auto validEntry = [](DPIABIEntry abi) {
        if (abi.kind > 13 || abi.direction > 3 || abi.width == 0 ||
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
        case 8:
        case 9:
          return abi.width == 64 && !fourState && (abi.flags & 2) == 0;
        case 10:
          return abi.width == 32 && !fourState && (abi.flags & 2) == 0;
        case 11:
          return abi.width == 64 && !fourState && (abi.flags & 2) == 0;
        case 12:
          return (abi.flags & 2) == 0;
        case 13:
          return true;
        }
        return false;
      };
      auto sameValue = [](DPIABIEntry left, DPIABIEntry right) {
        return left.kind == right.kind && left.width == right.width &&
               left.flags == right.flags;
      };
      auto matchesLayout = [&](DPIABIEntry abi, Layout layout, uint64_t index) {
        if (abi.kind == 12) {
          if (!dpiOpenLayouts[index])
            return false;
          const DPIOpenLayout &open = *dpiOpenLayouts[index];
          if (layout.width != open.transportWidth)
            return false;
          if (open.storage == 1)
            return layout.kind == OBELISK_RT_DBREG_MANAGED;
          if (open.transportFourState != 0)
            return layout.kind == OBELISK_RT_DBREG_LOGIC;
          // Recursive provenance can be rooted in a managed dynamic/queue
          // handle or in a fixed two-state aggregate of managed handles.
          return layout.kind == OBELISK_RT_DBREG_BITS ||
                 (open.storage == 2 && layout.kind == OBELISK_RT_DBREG_MANAGED);
        }
        if (abi.kind == 8)
          return layout.kind == OBELISK_RT_DBREG_STRING && layout.width == 64;
        if (abi.kind == 9)
          return layout.kind == OBELISK_RT_DBREG_BITS && layout.width == 64;
        if (abi.kind == 10)
          return layout.kind == OBELISK_RT_DBREG_REAL32 && layout.width == 32;
        if (abi.kind == 11)
          return layout.kind == OBELISK_RT_DBREG_REAL64 && layout.width == 64;
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
      dpiEntries.reserve(static_cast<size_t>(entryCount));
      for (uint64_t index = 0; index != entryCount; ++index) {
        DPIABIEntry abi = entry(index);
        if (!validEntry(abi) ||
            ((abi.kind == 12) != dpiOpenLayouts[index].has_value()) ||
            ((abi.kind == 13) != dpiAggregateLayouts[index].has_value()))
          return OBELISK_RT_INVALID_BYTECODE;
        dpiEntries.push_back(abi);
        appendHash(abi.kind, 4);
        appendHash(abi.direction, 4);
        appendHash(abi.width, 4);
        appendHash((abi.flags & 1) != 0, 1);
        appendHash((abi.flags & 2) != 0, 1);
      }
      if (hash == 0)
        hash = 1;
      if (importSite.abi_signature != 0 && hash != importSite.abi_signature)
        return OBELISK_RT_INVALID_BYTECODE;
      for (uint32_t index = 0; index != logicalInputs; ++index) {
        DPIABIEntry abi = entry(index);
        if (abi.direction == 3 ||
            !matchesLayout(
                abi, layoutAt(image, frame.function, inputRegister(index + 1)),
                index))
          return OBELISK_RT_INVALID_BYTECODE;
        dpiInputFlags.push_back((abi.flags & 2) != 0 ? OBELISK_RT_DBREG_SIGNED
                                                     : 0);
      }
      for (uint32_t index = 0; index != logicalOutputs; ++index) {
        DPIABIEntry abi = entry(uint64_t{logicalInputs} + index);
        if (!matchesLayout(
                abi, layoutAt(image, frame.function, outputRegister(index)),
                uint64_t{logicalInputs} + index))
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
        DPIABIEntry input = entry(index);
        if (input.direction == 0)
          continue;
        if (outputCursor >= entryCount)
          return OBELISK_RT_INVALID_BYTECODE;
        DPIABIEntry output = entry(outputCursor++);
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
    std::vector<std::optional<DPIOpenStorage>> dpiOpenInputs(inputCount);
    std::vector<int64_t> dpiOpenOutputSources(dataOutputCount, -1);
    std::vector<std::optional<DPIAggregateStorage>> dpiAggregateInputs(
        inputCount);
    std::vector<int64_t> dpiAggregateOutputSources(dataOutputCount, -1);
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
    auto prepareOpenInput = [&](uint32_t index,
                                Layout registerLayout) -> obelisk_rt_status {
      const DPIOpenLayout &layout = *dpiOpenLayouts[index];
      dpiOpenInputs[index].emplace();
      DPIOpenStorage &storage = *dpiOpenInputs[index];
      if (layout.storage == 2) {
        if (dpiEntries[index].direction == 1 || layout.shapePlan.empty())
          return OBELISK_RT_INVALID_BYTECODE;
        uint64_t rootPlaneSize = limbCount(registerLayout.width) * uint64_t{8};
        uint8_t *address = frame.data + registerLayout.offset;
        obelisk_rt_dpi_open_array_storage_v1 prepared{};
        obelisk_rt_status recursiveStatus =
            designBytecodeDpiOpenPrepareRecursive(
                address,
                layout.transportFourState ? address + rootPlaneSize : nullptr,
                rootPlaneSize, layout.transportWidth, layout.transportFourState,
                dpiEntries[index].direction != 0 ? 1 : 0, layout.elementKind,
                layout.elementWidth, layout.fourState,
                static_cast<int32_t>(layout.packedLeft),
                static_cast<int32_t>(layout.packedRight), layout.elementCSize,
                layout.elementCAlignment, layout.elementStringCount,
                layout.elementPlan.data(), layout.elementPlan.size(),
                layout.shapePlan.data(),
                static_cast<uint32_t>(layout.ranges.size() / 2), &prepared);
        if (recursiveStatus != OBELISK_RT_OK)
          return recursiveStatus;
        storage.descriptor = prepared.descriptor;
        storage.recursiveAllocation.reset(prepared.allocation);
        storage.elementSize = layout.elementCSize;
        storage.dataSize = storage.descriptor.data_size;
        return OBELISK_RT_OK;
      }
      storage.dynamic = layout.storage == 1;
      storage.elementSize = layout.elementCSize;
      if (storage.elementSize == 0)
        return OBELISK_RT_INVALID_BYTECODE;

      if (storage.dynamic) {
        storage.container = readManaged(inputRegister(index + firstInput));
        storage.elementCount = obelisk_rt_v1_container_size(storage.container);
        storage.totalWidth = storage.elementCount * layout.elementWidth;
        if (layout.elementWidth != 0 &&
            storage.totalWidth / layout.elementWidth != storage.elementCount)
          return OBELISK_RT_INVALID_BYTECODE;
        storage.planeSize = (storage.totalWidth + 7) / 8;
      } else {
        storage.elementCount = 1;
        for (size_t range = 0; range != layout.ranges.size(); range += 2) {
          int64_t left = layout.ranges[range];
          int64_t right = layout.ranges[range + 1];
          uint64_t extent = static_cast<uint64_t>(std::max(left, right) -
                                                  std::min(left, right)) +
                            1;
          if (extent == 0 || storage.elementCount > UINT64_MAX / extent)
            return OBELISK_RT_INVALID_BYTECODE;
          storage.elementCount *= extent;
        }
        storage.totalWidth = layout.transportWidth;
        storage.planeSize = uint64_t{limbCount(registerLayout.width)} * 8;
        if (storage.elementCount > UINT64_MAX / layout.elementWidth ||
            storage.elementCount * layout.elementWidth != storage.totalWidth)
          return OBELISK_RT_INVALID_BYTECODE;
      }
      if (storage.elementCount > UINT64_MAX / storage.elementSize ||
          storage.elementCount * storage.elementSize > SIZE_MAX ||
          storage.planeSize > SIZE_MAX)
        return OBELISK_RT_INVALID_BYTECODE;
      storage.dataSize = storage.elementCount * storage.elementSize;
      if (layout.elementStringCount != 0 &&
          storage.elementCount > UINT64_MAX / layout.elementStringCount)
        return OBELISK_RT_INVALID_BYTECODE;
      uint64_t strings = storage.elementCount * layout.elementStringCount;
      if (strings > (UINT64_MAX - storage.dataSize) / 8 ||
          storage.dataSize + strings * 8 > SIZE_MAX)
        return OBELISK_RT_INVALID_BYTECODE;
      uint64_t capacity = storage.dataSize + strings * 8;
      storage.data.resize(static_cast<size_t>(capacity));

      if (storage.dynamic) {
        if (storage.elementCount != 0) {
          storage.flatValue.resize(static_cast<size_t>(storage.planeSize));
          if (layout.fourState)
            storage.flatUnknown.resize(static_cast<size_t>(storage.planeSize));
          if (dpiEntries[index].direction != 1) {
            obelisk_rt_status status = obelisk_rt_v1_container_export_fixed(
                storage.container, storage.flatValue.data(),
                layout.fourState ? storage.flatUnknown.data() : nullptr,
                storage.planeSize, storage.totalWidth, layout.fourState,
                layout.elementWidth, storage.elementCount);
            if (status != OBELISK_RT_OK)
              return status;
          }
          obelisk_rt_status status = designBytecodeDpiOpenAggregatePack(
              storage.flatValue.data(),
              layout.fourState ? storage.flatUnknown.data() : nullptr,
              storage.planeSize, storage.totalWidth, layout.fourState,
              layout.elementWidth, storage.elementCount, 0, nullptr, 0,
              storage.data.data(), storage.dataSize, capacity,
              layout.elementCSize, layout.elementStringCount,
              layout.elementPlan.data(), layout.elementPlan.size());
          if (status != OBELISK_RT_OK)
            return status;
        }
      } else if (dpiEntries[index].direction != 1) {
        uint8_t *address = frame.data + registerLayout.offset;
        uint64_t *unknown =
            layout.fourState
                ? reinterpret_cast<uint64_t *>(address + storage.planeSize)
                : nullptr;
        obelisk_rt_status status = designBytecodeDpiOpenAggregatePack(
            address, unknown, storage.planeSize, storage.totalWidth,
            layout.fourState, layout.elementWidth, storage.elementCount, 0,
            layout.sourceRanges.data(),
            static_cast<uint32_t>(layout.sourceRanges.size() / 2),
            storage.data.data(), storage.dataSize, capacity,
            layout.elementCSize, layout.elementStringCount,
            layout.elementPlan.data(), layout.elementPlan.size());
        if (status != OBELISK_RT_OK)
          return status;
      }

      uint64_t dimensionCount = layout.ranges.size() / 2;
      if (storage.dynamic)
        dimensionCount = 1;
      storage.dimensions.resize(static_cast<size_t>(dimensionCount));
      uint64_t stride = storage.elementSize;
      for (uint64_t dimension = dimensionCount; dimension-- != 0;) {
        int64_t left = storage.dynamic ? 0 : layout.ranges[dimension * 2];
        int64_t right =
            storage.dynamic
                ? (storage.elementCount == 0
                       ? -1
                       : static_cast<int64_t>(storage.elementCount - 1))
                : layout.ranges[dimension * 2 + 1];
        if (left < INT32_MIN || left > INT32_MAX || right < INT32_MIN ||
            right > INT32_MAX)
          return OBELISK_RT_INVALID_BYTECODE;
        storage.dimensions[dimension] = {
            static_cast<int32_t>(left), static_cast<int32_t>(right), stride,
            storage.dynamic ? OBELISK_RT_DPI_DIMENSION_RUNTIME |
                                  (storage.elementCount == 0
                                       ? OBELISK_RT_DPI_DIMENSION_EMPTY
                                       : 0)
                            : 0,
            0};
        uint64_t extent = storage.dynamic
                              ? storage.elementCount
                              : static_cast<uint64_t>(std::max(left, right) -
                                                      std::min(left, right)) +
                                    1;
        if (dimension != 0 && extent != 0 && stride > UINT64_MAX / extent)
          return OBELISK_RT_INVALID_BYTECODE;
        stride *= extent;
      }
      bool packed = layout.elementKind <= 7;
      bool cLayout = true;
      uint32_t flags =
          (dpiEntries[index].direction != 0 ? OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE
                                            : 0) |
          (cLayout ? OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT : 0) |
          (packed ? OBELISK_RT_DPI_OPEN_ARRAY_PACKED : 0) |
          (layout.fourState ? OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE : 0) |
          (storage.dynamic && storage.elementCount == 0
               ? OBELISK_RT_DPI_OPEN_ARRAY_EMPTY
               : 0);
      storage.descriptor = {
          OBELISK_RT_DPI_OPEN_ARRAY_MAGIC,
          flags,
          static_cast<uint32_t>(dimensionCount),
          layout.elementWidth,
          static_cast<int32_t>(layout.packedLeft),
          static_cast<int32_t>(layout.packedRight),
          storage.elementSize,
          storage.data.empty() ? nullptr : storage.data.data(),
          storage.dataSize,
          storage.dimensions.empty() ? nullptr : storage.dimensions.data(),
          0};
      return OBELISK_RT_OK;
    };
    auto prepareAggregateInput =
        [&](uint32_t index, Layout registerLayout) -> obelisk_rt_status {
      const DPIAggregateLayout &layout = *dpiAggregateLayouts[index];
      uint64_t capacity = layout.cSize + layout.stringCount * uint64_t{8};
      if (capacity > SIZE_MAX)
        return OBELISK_RT_INVALID_BYTECODE;
      dpiAggregateInputs[index].emplace();
      DPIAggregateStorage &storage = *dpiAggregateInputs[index];
      storage.data.resize(static_cast<size_t>(capacity));
      if (dpiEntries[index].direction == 1)
        return OBELISK_RT_OK;
      uint64_t planeSize = limbCount(registerLayout.width) * uint64_t{8};
      uint8_t *address = frame.data + registerLayout.offset;
      const void *unknown =
          (dpiEntries[index].flags & 1) != 0 ? address + planeSize : nullptr;
      return designBytecodeDpiAggregatePack(
          address, unknown, planeSize, dpiEntries[index].width,
          dpiEntries[index].flags & 1, storage.data.data(), layout.cSize,
          capacity, layout.plan.data(), layout.plan.size());
    };
    for (uint32_t index = 0; index != inputCount; ++index) {
      Layout layout =
          layoutAt(image, frame.function, inputRegister(index + firstInput));
      if (signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT &&
          dpiEntries[index].kind == 12) {
        obelisk_rt_status status = prepareOpenInput(index, layout);
        if (status != OBELISK_RT_OK)
          return status;
        auto *descriptor = &dpiOpenInputs[index]->descriptor;
        inputs.push_back({OBELISK_RT_DBREG_OPEN_ARRAY, 0, 0, 64,
                          reinterpret_cast<uint64_t *>(descriptor), nullptr,
                          1});
        continue;
      }
      if (signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT &&
          dpiEntries[index].kind == 13) {
        obelisk_rt_status status = prepareAggregateInput(index, layout);
        if (status != OBELISK_RT_OK)
          return status;
        inputs.push_back({OBELISK_RT_DBREG_AGGREGATE, 0, 0, 64,
                          reinterpret_cast<uint64_t *>(
                              dpiAggregateInputs[index]->data.data()),
                          nullptr, 1});
        continue;
      }
      auto [width, limbs, value, unknown] =
          describe(layout, frame.data + layout.offset);
      uint8_t flags = signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT
                          ? dpiInputFlags[index]
                          : layout.flags;
      inputs.push_back({layout.kind, flags, 0, width, value, unknown, limbs});
    }
    if (signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT) {
      uint64_t outputCursor = 0;
      bool task = (importSite.flags & OBELISK_RT_IMPORT_TASK) != 0;
      if (!task && dataOutputCount != 0 &&
          dpiEntries[inputCount].direction == 3)
        outputCursor = 1;
      for (uint32_t input = 0; input != inputCount; ++input) {
        if (dpiEntries[input].direction == 0)
          continue;
        if (outputCursor >= dataOutputCount)
          return OBELISK_RT_INVALID_BYTECODE;
        if (dpiEntries[input].kind == 12)
          dpiOpenOutputSources[outputCursor] = input;
        if (dpiEntries[input].kind == 13)
          dpiAggregateOutputSources[outputCursor] = input;
        ++outputCursor;
      }
    }
    for (uint32_t index = 0; index != dataOutputCount; ++index) {
      Layout layout = layoutAt(image, frame.function, outputRegister(index));
      if (signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT &&
          dpiEntries[inputCount + index].kind == 12) {
        int64_t source = dpiOpenOutputSources[index];
        if (source < 0 || !dpiOpenInputs[static_cast<size_t>(source)])
          return OBELISK_RT_INVALID_BYTECODE;
        auto *descriptor =
            &dpiOpenInputs[static_cast<size_t>(source)]->descriptor;
        outputs.push_back({OBELISK_RT_DBREG_OPEN_ARRAY, 0, 0, 64,
                           reinterpret_cast<uint64_t *>(descriptor), nullptr,
                           1});
        continue;
      }
      if (signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT &&
          dpiEntries[inputCount + index].kind == 13) {
        int64_t source = dpiAggregateOutputSources[index];
        if (source < 0 || !dpiAggregateInputs[static_cast<size_t>(source)])
          return OBELISK_RT_INVALID_BYTECODE;
        outputs.push_back(
            {OBELISK_RT_DBREG_AGGREGATE, 0, 0, 64,
             reinterpret_cast<uint64_t *>(
                 dpiAggregateInputs[static_cast<size_t>(source)]->data.data()),
             nullptr, 1});
        continue;
      }
      uint8_t *address = frame.data + layout.offset;
      auto [width, limbs, value, unknown] = describe(layout, address);
      uint8_t flags = signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT
                          ? dpiOutputFlags[index]
                          : layout.flags;
      outputs.push_back({layout.kind, flags, 0, width, value, unknown, limbs});
    }
    obelisk_rt_status importStatus =
        (importSite.flags & OBELISK_RT_IMPORT_CONTEXT) != 0
            ? obelisk_rt_v1_import_call(context, &importSite, inputs.data(),
                                        inputCount, outputs.data(),
                                        dataOutputCount)
            : obelisk_rt_v1_import_call_noncontext(
                  context, &importSite, inputs.data(), inputCount,
                  outputs.data(), dataOutputCount);
    if (signature.id != OBELISK_RT_INTRINSIC_V1_DPI_IMPORT)
      return importStatus;
    if (importStatus == OBELISK_RT_OK) {
      for (uint32_t index = 0; index != dataOutputCount; ++index) {
        int64_t aggregateSource = dpiAggregateOutputSources[index];
        if (aggregateSource >= 0) {
          size_t sourceIndex = static_cast<size_t>(aggregateSource);
          DPIAggregateStorage &storage = *dpiAggregateInputs[sourceIndex];
          const DPIAggregateLayout &aggregate =
              *dpiAggregateLayouts[sourceIndex];
          Layout output =
              layoutAt(image, frame.function, outputRegister(index));
          uint64_t planeSize = limbCount(output.width) * uint64_t{8};
          uint8_t *address = frame.data + output.offset;
          void *unknown = (dpiEntries[inputCount + index].flags & 1) != 0
                              ? address + planeSize
                              : nullptr;
          obelisk_rt_status status = designBytecodeDpiAggregateUnpack(
              context, storage.data.data(), aggregate.cSize,
              aggregate.plan.data(), aggregate.plan.size(), address, unknown,
              planeSize, dpiEntries[inputCount + index].width,
              dpiEntries[inputCount + index].flags & 1);
          if (status != OBELISK_RT_OK)
            return status;
          continue;
        }
        int64_t source = dpiOpenOutputSources[index];
        if (source < 0)
          continue;
        DPIOpenStorage &storage = *dpiOpenInputs[static_cast<size_t>(source)];
        const DPIOpenLayout &open =
            *dpiOpenLayouts[static_cast<size_t>(source)];
        Layout output = layoutAt(image, frame.function, outputRegister(index));
        if (open.storage == 2) {
          Layout input = layoutAt(
              image, frame.function,
              inputRegister(static_cast<uint32_t>(source) + firstInput));
          uint64_t rootPlaneSize = limbCount(input.width) * uint64_t{8};
          uint8_t *inputAddress = frame.data + input.offset;
          obelisk_rt_dpi_open_array_storage_v1 recursive{
              storage.descriptor, storage.recursiveAllocation.get()};
          obelisk_rt_status status = designBytecodeDpiOpenFinishRecursive(
              OBELISK_RT_OK, context, &recursive, inputAddress,
              open.transportFourState ? inputAddress + rootPlaneSize : nullptr,
              rootPlaneSize, open.transportWidth, open.transportFourState,
              open.elementWidth, open.fourState, open.elementCSize,
              open.elementPlan.data(), open.elementPlan.size(),
              open.shapePlan.data(),
              static_cast<uint32_t>(open.ranges.size() / 2));
          if (status != OBELISK_RT_OK || input.size != output.size)
            return status != OBELISK_RT_OK ? status
                                           : OBELISK_RT_INVALID_BYTECODE;
          std::memcpy(frame.data + output.offset, inputAddress, input.size);
          continue;
        }
        if (storage.dynamic) {
          if (!writeManaged(outputRegister(index), storage.container))
            return OBELISK_RT_INVALID_BYTECODE;
          if (storage.elementCount == 0)
            continue;
          obelisk_rt_status status = designBytecodeDpiOpenAggregateUnpack(
              context, storage.data.data(), storage.dataSize, open.elementCSize,
              open.elementPlan.data(), open.elementPlan.size(),
              storage.elementCount, 0, nullptr, 0, storage.flatValue.data(),
              open.fourState ? storage.flatUnknown.data() : nullptr,
              storage.planeSize, storage.totalWidth, open.fourState,
              open.elementWidth);
          if (status != OBELISK_RT_OK)
            return status;
          void *aggregateRoots = nullptr;
          if (open.elementStringCount != 0) {
            status = designBytecodeDpiOpenAggregateRootsPush(
                context, storage.flatValue.data(), storage.planeSize,
                storage.totalWidth, open.elementWidth, storage.elementCount, 0,
                nullptr, 0, open.elementCSize, open.elementPlan.data(),
                open.elementPlan.size(), &aggregateRoots);
            if (status != OBELISK_RT_OK)
              return status;
          }
          obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
          if (!lane) {
            if (aggregateRoots)
              (void)designBytecodeDpiAggregateRootsPop(context, aggregateRoots);
            return OBELISK_RT_INVALID_LIFECYCLE;
          }
          status = obelisk_rt_v1_container_import_fixed(
              lane, storage.container, storage.flatValue.data(),
              open.fourState ? storage.flatUnknown.data() : nullptr,
              storage.planeSize, storage.totalWidth, open.fourState,
              open.elementWidth, storage.elementCount);
          obelisk_rt_status popStatus =
              designBytecodeDpiAggregateRootsPop(context, aggregateRoots);
          if (status != OBELISK_RT_OK)
            return status;
          if (popStatus != OBELISK_RT_OK)
            return popStatus;
          continue;
        }
        uint8_t *address = frame.data + output.offset;
        obelisk_rt_status status = designBytecodeDpiOpenAggregateUnpack(
            context, storage.data.data(), storage.dataSize, open.elementCSize,
            open.elementPlan.data(), open.elementPlan.size(),
            storage.elementCount, 0, open.sourceRanges.data(),
            static_cast<uint32_t>(open.sourceRanges.size() / 2), address,
            open.fourState ? address + storage.planeSize : nullptr,
            storage.planeSize, storage.totalWidth, open.fourState,
            open.elementWidth);
        if (status != OBELISK_RT_OK)
          return status;
      }
    }
    Layout statusLayout =
        layoutAt(image, frame.function, outputRegister(dataOutputCount));
    uint8_t *statusAddress = frame.data + statusLayout.offset;
    std::memset(statusAddress, 0, statusLayout.size);
    uint64_t statusBits = static_cast<uint32_t>(importStatus);
    std::memcpy(statusAddress, &statusBits, sizeof(statusBits));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_FORMAT:
  case OBELISK_RT_INTRINSIC_V1_DISPLAY: {
    bool stringOutput = signature.id == OBELISK_RT_INTRINSIC_V1_FORMAT;
    auto metadata = bytes(0);
    auto descriptor = stringOutput ? std::optional<uint64_t>(0) : scalar(1);
    if (!metadata || !descriptor || *descriptor > UINT32_MAX ||
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
    uint32_t physical = stringOutput ? 1 : 2;
    std::vector<Logic> values;
    values.reserve(site.inputCount - physical);
    std::vector<double> realValues;
    realValues.reserve(itemCount);
    std::vector<obelisk_rt_arg_v1> arguments;
    arguments.reserve(itemCount);
    std::vector<obelisk_rt_enum_arg_v1> enumArguments;
    enumArguments.reserve(itemCount);
    std::vector<obelisk_rt_net_arg_v1> netArguments;
    netArguments.reserve(itemCount);
    std::vector<obelisk_rt_raw_aggregate_arg_v1> rawAggregateArguments;
    rawAggregateArguments.reserve(itemCount);
    for (uint32_t index = 0; index != itemCount; ++index) {
      uint32_t itemFlags = read32(flags + uint64_t{index} * 4);
      if ((itemFlags & ~uint32_t{OBELISK_RT_OUTPUT_ITEM_ALL}) != 0 ||
          ((itemFlags & OBELISK_RT_OUTPUT_ITEM_REAL) != 0 &&
           (itemFlags & (OBELISK_RT_OUTPUT_ITEM_SIGNED |
                         OBELISK_RT_OUTPUT_ITEM_OMITTED)) != 0))
        return OBELISK_RT_INVALID_BYTECODE;
      if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_OMITTED) != 0) {
        arguments.push_back({OBELISK_RT_ARG_EMPTY, 0, 0, nullptr, nullptr});
        continue;
      }
      if (physical >= site.inputCount)
        return OBELISK_RT_INVALID_BYTECODE;
      uint32_t reg = inputRegister(physical++);
      Layout layout = layoutAt(image, frame.function, reg);
      if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_ENUM) != 0) {
        if (itemFlags != OBELISK_RT_OUTPUT_ITEM_ENUM &&
            itemFlags !=
                (OBELISK_RT_OUTPUT_ITEM_ENUM | OBELISK_RT_OUTPUT_ITEM_SIGNED))
          return OBELISK_RT_INVALID_BYTECODE;
        if (physical >= site.inputCount ||
            (layout.kind != OBELISK_RT_DBREG_BITS &&
             layout.kind != OBELISK_RT_DBREG_LOGIC))
          return OBELISK_RT_INVALID_BYTECODE;
        uint32_t nameReg = inputRegister(physical++);
        Layout nameLayout = layoutAt(image, frame.function, nameReg);
        if (nameLayout.kind != OBELISK_RT_DBREG_STRING || nameLayout.size != 8)
          return OBELISK_RT_INVALID_BYTECODE;
        values.push_back(readLogic(frame.data, layout));
        Logic &value = values.back();
        obelisk_rt_string_v1 name = 0;
        std::memcpy(&name, frame.data + nameLayout.offset, sizeof(name));
        enumArguments.push_back(
            {value.width,
             static_cast<uint32_t>((itemFlags & OBELISK_RT_OUTPUT_ITEM_SIGNED)
                                       ? OBELISK_RT_ARG_SIGNED
                                       : 0),
             0, value.value.data(),
             value.fourState ? value.unknown.data() : nullptr, name});
        arguments.push_back({OBELISK_RT_ARG_ENUM,
                             static_cast<obelisk_rt_arg_flags>(
                                 (itemFlags & OBELISK_RT_OUTPUT_ITEM_SIGNED)
                                     ? OBELISK_RT_ARG_SIGNED
                                     : 0),
                             0, &enumArguments.back(), nullptr});
      } else if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_NET) != 0) {
        if (itemFlags != OBELISK_RT_OUTPUT_ITEM_NET &&
            itemFlags !=
                (OBELISK_RT_OUTPUT_ITEM_NET | OBELISK_RT_OUTPUT_ITEM_SIGNED))
          return OBELISK_RT_INVALID_BYTECODE;
        if (physical >= site.inputCount ||
            (layout.kind != OBELISK_RT_DBREG_BITS &&
             layout.kind != OBELISK_RT_DBREG_LOGIC))
          return OBELISK_RT_INVALID_BYTECODE;
        uint32_t handleReg = inputRegister(physical++);
        Layout handleLayout = layoutAt(image, frame.function, handleReg);
        if (handleLayout.kind != OBELISK_RT_DBREG_HANDLE)
          return OBELISK_RT_INVALID_BYTECODE;
        uint64_t stable = UINT64_MAX;
        if (!encodeCanonicalHandle(frame.data + handleLayout.offset, stable))
          return OBELISK_RT_INVALID_HANDLE;
        values.push_back(readLogic(frame.data, layout));
        Logic &value = values.back();
        netArguments.push_back(
            {value.width,
             static_cast<uint32_t>((itemFlags & OBELISK_RT_OUTPUT_ITEM_SIGNED)
                                       ? OBELISK_RT_ARG_SIGNED
                                       : 0),
             context->nativeSchedulePlan && !context->nativeScheduleDeoptimized
                 ? 1u
                 : 0u,
             value.value.data(),
             value.fourState ? value.unknown.data() : nullptr, stable});
        arguments.push_back({OBELISK_RT_ARG_NET,
                             static_cast<obelisk_rt_arg_flags>(
                                 (itemFlags & OBELISK_RT_OUTPUT_ITEM_SIGNED)
                                     ? OBELISK_RT_ARG_SIGNED
                                     : 0),
                             0, &netArguments.back(), nullptr});
      } else if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_RAW_AGGREGATE) != 0) {
        if (itemFlags != OBELISK_RT_OUTPUT_ITEM_RAW_AGGREGATE ||
            physical + 1 >= site.inputCount)
          return OBELISK_RT_INVALID_BYTECODE;
        obelisk_rt_string_v1 strings[3] = {};
        Layout stringLayouts[3] = {
            layout, layoutAt(image, frame.function, inputRegister(physical++)),
            layoutAt(image, frame.function, inputRegister(physical++))};
        for (unsigned ordinal = 0; ordinal != 3; ++ordinal) {
          if (stringLayouts[ordinal].kind != OBELISK_RT_DBREG_STRING ||
              stringLayouts[ordinal].size != sizeof(obelisk_rt_string_v1))
            return OBELISK_RT_INVALID_BYTECODE;
          std::memcpy(&strings[ordinal],
                      frame.data + stringLayouts[ordinal].offset,
                      sizeof(strings[ordinal]));
        }
        rawAggregateArguments.push_back({strings[0], strings[1], strings[2]});
        arguments.push_back({OBELISK_RT_ARG_RAW_AGGREGATE, 0, 0,
                             &rawAggregateArguments.back(), nullptr});
      } else if (layout.kind == OBELISK_RT_DBREG_BYTES) {
        auto value = readByteSpan(image, frame, reg);
        if (!value || (itemFlags != 0 &&
                       itemFlags != OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT))
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back(
            {OBELISK_RT_ARG_STRING,
             static_cast<obelisk_rt_arg_flags>(
                 OBELISK_RT_ARG_FORMAT_STRING |
                 ((itemFlags & OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT)
                      ? OBELISK_RT_ARG_DESIGNATED_FORMAT
                      : 0)),
             value->size, value->data, nullptr});
      } else if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_STRING) != 0) {
        if (layout.kind != OBELISK_RT_DBREG_STRING || layout.size != 8 ||
            (itemFlags != OBELISK_RT_OUTPUT_ITEM_STRING &&
             itemFlags != (OBELISK_RT_OUTPUT_ITEM_STRING |
                           OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT) &&
             itemFlags != (OBELISK_RT_OUTPUT_ITEM_STRING |
                           OBELISK_RT_OUTPUT_ITEM_FORMAT)))
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back(
            {OBELISK_RT_ARG_MANAGED_STRING,
             static_cast<obelisk_rt_arg_flags>(
                 ((itemFlags & (OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT |
                                OBELISK_RT_OUTPUT_ITEM_FORMAT))
                      ? obelisk_rt_arg_flags{OBELISK_RT_ARG_FORMAT_STRING}
                      : obelisk_rt_arg_flags{0}) |
                 ((itemFlags & OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT)
                      ? obelisk_rt_arg_flags{OBELISK_RT_ARG_DESIGNATED_FORMAT}
                      : obelisk_rt_arg_flags{0})),
             0, frame.data + layout.offset, nullptr});
      } else if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_CONTAINER) != 0) {
        if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8 ||
            itemFlags != OBELISK_RT_OUTPUT_ITEM_CONTAINER)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back({OBELISK_RT_ARG_MANAGED_CONTAINER, 0, 0,
                             frame.data + layout.offset, nullptr});
      } else if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_CLASS) != 0) {
        if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8 ||
            itemFlags != OBELISK_RT_OUTPUT_ITEM_CLASS)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back({OBELISK_RT_ARG_MANAGED_OBJECT, 0, 0,
                             frame.data + layout.offset, nullptr});
      } else if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_VIRTUAL_INTERFACE) != 0) {
        if (itemFlags != OBELISK_RT_OUTPUT_ITEM_VIRTUAL_INTERFACE ||
            layout.kind != OBELISK_RT_DBREG_BITS || layout.width != 64 ||
            layout.size != 8)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back({OBELISK_RT_ARG_VIRTUAL_INTERFACE, 0, 0,
                             frame.data + layout.offset, nullptr});
      } else if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_PROCESS) != 0) {
        if (itemFlags != OBELISK_RT_OUTPUT_ITEM_PROCESS ||
            layout.kind != OBELISK_RT_DBREG_BITS || layout.width != 64 ||
            layout.size != 8)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back({OBELISK_RT_ARG_PROCESS, 0, 0,
                             frame.data + layout.offset, nullptr});
      } else if ((itemFlags & OBELISK_RT_OUTPUT_ITEM_REAL) != 0) {
        if (itemFlags != OBELISK_RT_OUTPUT_ITEM_REAL)
          return OBELISK_RT_INVALID_BYTECODE;
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
        if (itemFlags != 0 && itemFlags != OBELISK_RT_OUTPUT_ITEM_SIGNED)
          return OBELISK_RT_INVALID_BYTECODE;
        values.push_back(readLogic(frame.data, layout));
        Logic &value = values.back();
        arguments.push_back({OBELISK_RT_ARG_LOGIC,
                             static_cast<obelisk_rt_arg_flags>(
                                 (itemFlags & OBELISK_RT_OUTPUT_ITEM_SIGNED)
                                     ? OBELISK_RT_ARG_SIGNED
                                     : 0),
                             value.width, value.value.data(),
                             value.fourState ? value.unknown.data() : nullptr});
      }
    }
    if (physical != site.inputCount)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_format_env_v1 environment{scope,       scopeSize, library,
                                         librarySize, 0,         precision,
                                         nullptr,     0,         multiplier};
    if (!stringOutput)
      return obelisk_rt_v1_display(
          context, static_cast<uint32_t>(*descriptor), newline,
          static_cast<obelisk_rt_radix>(radix), arguments.data(),
          arguments.size(), &environment);
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_output_format(
        context, static_cast<obelisk_rt_radix>(radix), arguments.data(),
        arguments.size(), &environment, &result);
    if (status != OBELISK_RT_OK)
      return status;
    return writeString(outputRegister(0), result) ? OBELISK_RT_OK
                                                  : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_FINISH:
  case OBELISK_RT_INTRINSIC_V1_STOP:
  case OBELISK_RT_INTRINSIC_V1_FATAL: {
    auto verbosity = scalar(0);
    if (!verbosity || *verbosity > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    if (signature.id == OBELISK_RT_INTRINSIC_V1_FINISH)
      return obelisk_rt_v1_scheduler_finish(context,
                                            static_cast<uint32_t>(*verbosity));
    if (signature.id == OBELISK_RT_INTRINSIC_V1_STOP)
      return obelisk_rt_v1_scheduler_stop(context,
                                          static_cast<uint32_t>(*verbosity));
    return obelisk_rt_v1_scheduler_fatal(context,
                                         static_cast<uint32_t>(*verbosity));
  }
  case OBELISK_RT_INTRINSIC_V1_ERROR:
    return obelisk_rt_v1_scheduler_error(context);
  case OBELISK_RT_INTRINSIC_V1_PROGRAM_EXIT:
    return obelisk_rt_v1_scheduler_program_exit(context);
  case OBELISK_RT_INTRINSIC_V1_TERMINATION_REQUESTED:
    return sentinel(0, obelisk_rt_v1_scheduler_termination_requested(context));
  case OBELISK_RT_INTRINSIC_V1_NET_COUNT_DRIVERS: {
    Layout net = layoutAt(image, frame.function, inputRegister(0));
    if (net.kind != OBELISK_RT_DBREG_HANDLE)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t stable = UINT64_MAX;
    if (!encodeCanonicalHandle(frame.data + net.offset, stable))
      return OBELISK_RT_INVALID_HANDLE;
    uint32_t forced = 0, total = 0, zero = 0, one = 0, unknown = 0;
    obelisk_rt_status query = obelisk_rt_count_design_drivers(
        context, stable, &forced, &total, &zero, &one, &unknown, false);
    if (query != OBELISK_RT_OK)
      return query;
    const uint32_t values[] = {forced, total, zero, one, unknown};
    for (uint32_t index = 0; index != 5; ++index) {
      uint32_t value = values[index];
      obelisk_rt_status written = sentinel(index, value);
      if (written != OBELISK_RT_OK)
        return written;
    }
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_PASS_SWITCH_CONTROL: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Logic control = readLogic(frame.data, input);
    if (control.width != 1 || control.value.empty())
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t value = static_cast<uint32_t>(control.value[0] & 1);
    uint32_t unknown = control.fourState && !control.unknown.empty()
                           ? static_cast<uint32_t>(control.unknown[0] & 1)
                           : 0;
    return obelisk_rt_v1_pass_switch_control(context, signature.flags, value,
                                             unknown);
  }
  case OBELISK_RT_INTRINSIC_V1_PASS_SWITCH_CONTROL_DELAYED: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Logic control = readLogic(frame.data, input);
    auto turnOn = scalar(1);
    auto turnOff = scalar(2);
    auto unknownDelay = scalar(3);
    if (control.width != 1 || control.value.empty() || !turnOn || !turnOff ||
        !unknownDelay)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t value = static_cast<uint32_t>(control.value[0] & 1);
    uint32_t unknown = control.fourState && !control.unknown.empty()
                           ? static_cast<uint32_t>(control.unknown[0] & 1)
                           : 0;
    return obelisk_rt_v1_pass_switch_control_delayed(context, signature.flags,
                                                     value, unknown, *turnOn,
                                                     *turnOff, *unknownDelay);
  }
  case OBELISK_RT_INTRINSIC_V1_MOS_DRIVE_DELAYED: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Logic control = readLogic(frame.data, input);
    auto rise = scalar(1);
    auto fall = scalar(2);
    auto turnoff = scalar(3);
    if (control.width != 1 || control.value.empty() || !rise || !fall ||
        !turnoff)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t value = static_cast<uint32_t>(control.value[0] & 1);
    uint32_t unknown = control.fourState && !control.unknown.empty()
                           ? static_cast<uint32_t>(control.unknown[0] & 1)
                           : 0;
    return obelisk_rt_v1_mos_drive_delayed(context, signature.flags, value,
                                           unknown, *rise, *fall, *turnoff);
  }
  case OBELISK_RT_INTRINSIC_V1_TIME_NOW:
    return sentinel(0, obelisk_rt_v1_scheduler_time(context));
  case OBELISK_RT_INTRINSIC_V1_SAMPLED_READ: {
    Layout source = layoutAt(image, frame.function, inputRegister(0));
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (source.kind != OBELISK_RT_DBREG_HANDLE)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t stable = UINT64_MAX;
    if (!encodeCanonicalHandle(frame.data + source.offset, stable))
      return OBELISK_RT_INVALID_HANDLE;
    Logic sampled{output.width, output.kind == OBELISK_RT_DBREG_LOGIC,
                  LimbVector(limbCount(output.width)),
                  LimbVector(limbCount(output.width))};
    obelisk_rt_status status = obelisk_rt_v1_sampled_read(
        context, stable, output.width,
        reinterpret_cast<uint8_t *>(sampled.value.data()),
        reinterpret_cast<uint8_t *>(sampled.unknown.data()));
    if (status == OBELISK_RT_OK)
      writeLogic(frame.data, output, sampled);
    return status;
  }
  case OBELISK_RT_INTRINSIC_V1_SAMPLED_HISTORY: {
    auto siteID = scalar(0), depth = scalar(1), gate = scalar(2);
    if (!siteID || !depth || !gate || *gate > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(3));
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    Logic current = readLogic(frame.data, input);
    Logic previous{output.width, output.kind == OBELISK_RT_DBREG_LOGIC,
                   LimbVector(limbCount(output.width)),
                   LimbVector(limbCount(output.width))};
    obelisk_rt_status status = obelisk_rt_v1_sampled_history(
        context, *siteID, current.width, *depth, current.fourState, *gate,
        reinterpret_cast<const uint8_t *>(current.value.data()),
        current.fourState
            ? reinterpret_cast<const uint8_t *>(current.unknown.data())
            : nullptr,
        reinterpret_cast<uint8_t *>(previous.value.data()),
        reinterpret_cast<uint8_t *>(previous.unknown.data()));
    if (status == OBELISK_RT_OK)
      writeLogic(frame.data, output, previous);
    return status;
  }
  case OBELISK_RT_INTRINSIC_V1_CLOCKED_SAMPLE_UPDATE: {
    auto siteID = scalar(0), depth = scalar(1), gate = scalar(2);
    if (!siteID || !depth || !gate || *gate > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(3));
    Logic current = readLogic(frame.data, input);
    return obelisk_rt_v1_clocked_sample_update(
        context, *siteID, current.width, *depth, current.fourState, *gate,
        reinterpret_cast<const uint8_t *>(current.value.data()),
        current.fourState
            ? reinterpret_cast<const uint8_t *>(current.unknown.data())
            : nullptr);
  }
  case OBELISK_RT_INTRINSIC_V1_CLOCKED_SAMPLE_READ: {
    auto siteID = scalar(0), depth = scalar(1), age = scalar(2);
    if (!siteID || !depth || !age)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    Logic sampled{output.width, output.kind == OBELISK_RT_DBREG_LOGIC,
                  LimbVector(limbCount(output.width)),
                  LimbVector(limbCount(output.width))};
    obelisk_rt_status status = obelisk_rt_v1_clocked_sample_read(
        context, *siteID, sampled.width, *depth, *age, sampled.fourState,
        reinterpret_cast<uint8_t *>(sampled.value.data()),
        reinterpret_cast<uint8_t *>(sampled.unknown.data()));
    if (status == OBELISK_RT_OK)
      writeLogic(frame.data, output, sampled);
    return status;
  }
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
  case OBELISK_RT_INTRINSIC_V1_LOGIC_CASE_DIFFERENCE_MASK: {
    Logic lhs = readLogic(frame.data,
                          layoutAt(image, frame.function, inputRegister(0)));
    Logic rhs = readLogic(frame.data,
                          layoutAt(image, frame.function, inputRegister(1)));
    if (lhs.width != rhs.width)
      return OBELISK_RT_INVALID_BYTECODE;
    Logic result{lhs.width, false, LimbVector(limbCount(lhs.width)), {}};
    for (size_t index = 0; index != result.value.size(); ++index)
      result.value[index] = (lhs.value[index] ^ rhs.value[index]) |
                            (lhs.unknown[index] ^ rhs.unknown[index]);
    if (!result.value.empty())
      result.value.back() &= finalMask(lhs.width);
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    writeLogic(frame.data, output, result);
    return OBELISK_RT_OK;
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
  case OBELISK_RT_INTRINSIC_V1_TIME_SCAN_SCALE: {
    auto input = realInput(0);
    auto multiplier = scalar(1), precision = scalar(2);
    if (!input || !multiplier || !precision || *multiplier == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return writeReal(
        0, obelisk_rt_v1_time_scan_scale(context, *input, *multiplier,
                                         static_cast<int32_t>(*precision)));
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
  case OBELISK_RT_INTRINSIC_V1_PLUSARG_SCAN: {
    obelisk_rt_string_v1 format = 0;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), format))
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 tail = 0;
    uint32_t conversion = 0;
    uint32_t found = 0;
    obelisk_rt_status status = obelisk_rt_v1_plusarg_scan(
        context, lane, format, &tail, &conversion, &found);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(outputRegister(0), tail))
      return OBELISK_RT_INVALID_BYTECODE;
    status = sentinel(1, conversion);
    return status == OBELISK_RT_OK ? sentinel(2, found) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_PLUSARG_PARSE_LOGIC: {
    obelisk_rt_string_v1 input = 0;
    auto radix = scalar(1);
    if (!readString(inputRegister(0), input) || !radix)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (output.kind != OBELISK_RT_DBREG_LOGIC || output.width == 0 ||
        output.size % 2 != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t planeSize = output.size / 2;
    // The bytecode register layout rounds each plane up for alignment. Keep
    // those non-value padding bytes deterministic as other logic intrinsics
    // do; the strict parser itself writes exactly ceil(width / 8) bytes.
    std::memset(frame.data + output.offset, 0, output.size);
    return obelisk_rt_v1_plusarg_parse_logic(
        input, static_cast<uint32_t>(*radix), output.width,
        frame.data + output.offset, planeSize,
        frame.data + output.offset + planeSize, planeSize);
  }
  case OBELISK_RT_INTRINSIC_V1_PLUSARG_PARSE_REAL: {
    obelisk_rt_string_v1 input = 0;
    if (!readString(inputRegister(0), input))
      return OBELISK_RT_INVALID_BYTECODE;
    double result = 0.0;
    obelisk_rt_status status = obelisk_rt_v1_plusarg_parse_real(input, &result);
    return status == OBELISK_RT_OK ? writeReal(0, result) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_SYSTEM: {
    obelisk_rt_string_v1 command = 0;
    if (!readString(inputRegister(0), command))
      return OBELISK_RT_INVALID_BYTECODE;
    int32_t result = -1;
    obelisk_rt_status status = obelisk_rt_v1_system(context, command, &result);
    return sentinel(0, status == OBELISK_RT_OK ? static_cast<uint32_t>(result)
                                               : UINT32_MAX);
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
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    // IEEE exposes these as void system tasks. A bad descriptor may update
    // the runtime's last-error record, but it must not terminate the calling
    // procedural process.
    if (signature.id == OBELISK_RT_INTRINSIC_V1_FILE_CLOSE)
      (void)obelisk_rt_v1_file_close(context,
                                     static_cast<uint32_t>(*descriptor));
    else
      (void)obelisk_rt_v1_file_flush(context,
                                     static_cast<uint32_t>(*descriptor));
    return OBELISK_RT_OK;
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
  case OBELISK_RT_INTRINSIC_V1_FILE_READMEM_TOKEN: {
    auto descriptor = scalar(0), radix = scalar(1);
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (!descriptor || !radix || *descriptor > UINT32_MAX ||
        (*radix != 2 && *radix != 16) ||
        output.kind != OBELISK_RT_DBREG_LOGIC || output.width == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t byteSize = (uint64_t{output.width} + 7) / 8;
    std::vector<uint8_t> value(static_cast<size_t>(byteSize));
    std::vector<uint8_t> unknown(static_cast<size_t>(byteSize));
    uint32_t kind = OBELISK_RT_READMEM_EOF;
    uint64_t address = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_readmem_token(
        context, static_cast<uint32_t>(*descriptor),
        static_cast<uint32_t>(*radix), output.width, value.data(), byteSize,
        unknown.data(), byteSize, &kind, &address);
    if (status != OBELISK_RT_OK)
      return status;
    Logic data{output.width, true, LimbVector(limbCount(output.width)),
               LimbVector(limbCount(output.width))};
    std::memcpy(data.value.data(), value.data(), static_cast<size_t>(byteSize));
    std::memcpy(data.unknown.data(), unknown.data(),
                static_cast<size_t>(byteSize));
    writeLogic(frame.data, output, data);
    status = sentinel(1, kind);
    return status == OBELISK_RT_OK ? sentinel(2, address) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STOCHASTIC_QUEUE: {
    auto action = scalar(0), unitScale = scalar(4);
    Layout idLayout = layoutAt(image, frame.function, inputRegister(1));
    Layout firstLayout = layoutAt(image, frame.function, inputRegister(2));
    Layout secondLayout = layoutAt(image, frame.function, inputRegister(3));
    Layout primaryLayout = layoutAt(image, frame.function, outputRegister(0));
    Layout secondaryLayout = layoutAt(image, frame.function, outputRegister(1));
    if (!action || !unitScale || *action > OBELISK_RT_STOCHASTIC_QUEUE_EXAM ||
        *unitScale == 0 ||
        (idLayout.kind != OBELISK_RT_DBREG_BITS &&
         idLayout.kind != OBELISK_RT_DBREG_LOGIC) ||
        idLayout.width != 32 ||
        (firstLayout.kind != OBELISK_RT_DBREG_BITS &&
         firstLayout.kind != OBELISK_RT_DBREG_LOGIC) ||
        firstLayout.width != 32 ||
        (secondLayout.kind != OBELISK_RT_DBREG_BITS &&
         secondLayout.kind != OBELISK_RT_DBREG_LOGIC) ||
        secondLayout.width != 32 ||
        primaryLayout.kind != OBELISK_RT_DBREG_LOGIC ||
        primaryLayout.width != 64 ||
        secondaryLayout.kind != OBELISK_RT_DBREG_LOGIC ||
        secondaryLayout.width != 64)
      return OBELISK_RT_INVALID_BYTECODE;
    Logic id = readLogic(frame.data, idLayout);
    Logic first = readLogic(frame.data, firstLayout);
    Logic second = readLogic(frame.data, secondLayout);
    uint64_t primaryValue = 0, primaryUnknown = 0;
    uint64_t secondaryValue = 0, secondaryUnknown = 0;
    obelisk_rt_stochastic_queue_status_v1 queueStatus = 0;
    obelisk_rt_status status = obelisk_rt_v1_stochastic_queue(
        context, static_cast<uint32_t>(*action),
        static_cast<uint32_t>(id.value[0]),
        static_cast<uint32_t>(id.unknown[0]),
        static_cast<uint32_t>(first.value[0]),
        static_cast<uint32_t>(first.unknown[0]),
        static_cast<uint32_t>(second.value[0]),
        static_cast<uint32_t>(second.unknown[0]), *unitScale, &primaryValue,
        &primaryUnknown, &secondaryValue, &secondaryUnknown, &queueStatus);
    if (status != OBELISK_RT_OK)
      return status;
    Logic primary{64, true, LimbVector(1), LimbVector(1)};
    primary.value[0] = primaryValue;
    primary.unknown[0] = primaryUnknown;
    Logic secondary{64, true, LimbVector(1), LimbVector(1)};
    secondary.value[0] = secondaryValue;
    secondary.unknown[0] = secondaryUnknown;
    writeLogic(frame.data, primaryLayout, primary);
    writeLogic(frame.data, secondaryLayout, secondary);
    return sentinel(2, queueStatus);
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
  case OBELISK_RT_INTRINSIC_V1_STRING_SCAN_DYNAMIC:
  case OBELISK_RT_INTRINSIC_V1_FILE_SCAN_DYNAMIC:
  case OBELISK_RT_INTRINSIC_V1_SCAN_DYNAMIC_VALIDATE:
    if (!invokeDynamicScanIntrinsic)
      return OBELISK_RT_INVALID_BYTECODE;
    return invokeDynamicScanIntrinsic(image, frame, context, site,
                                      signature.id);
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM:
  case OBELISK_RT_INTRINSIC_V1_AGGREGATE_EXPORT_BITSTREAM:
  case OBELISK_RT_INTRINSIC_V1_AGGREGATE_IMPORT_BITSTREAM:
    if (!invokeContainerBitstreamIntrinsic)
      return OBELISK_RT_INVALID_BYTECODE;
    return invokeContainerBitstreamIntrinsic(image, frame, context, site,
                                             siteIndex, signature.id);
  default:
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

} // namespace obelisk::designbytecode
