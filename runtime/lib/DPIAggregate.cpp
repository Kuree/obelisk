//===- DPIAggregate.cpp - Sized unpacked aggregate ABI -------------------===//

#include "RuntimeInternal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr uint64_t recordWords = 8;
static_assert(sizeof(void *) == 8,
              "the generated DPI aggregate ABI requires 64-bit pointers");

bool addMultiply(uint64_t base, uint64_t index, uint64_t stride,
                 uint64_t &result) {
  if (index != 0 && stride > (UINT64_MAX - base) / index)
    return false;
  result = base + index * stride;
  return true;
}

bool addSignedMultiply(uint64_t base, uint64_t index, int64_t stride,
                       uint64_t &result) {
  if (stride >= 0)
    return addMultiply(base, index, static_cast<uint64_t>(stride), result);
  uint64_t magnitude = uint64_t{0} - static_cast<uint64_t>(stride);
  if (index != 0 && magnitude > base / index)
    return false;
  result = base - index * magnitude;
  return true;
}

template <typename Visitor>
bool walkPlan(const int64_t *records, uint64_t recordCount, uint64_t bitBase,
              uint64_t byteBase, Visitor &visit) {
  if (!records)
    return false;
  struct Frame {
    const int64_t *records = nullptr;
    uint64_t recordCount = 0;
    uint64_t index = 0;
    uint64_t bitBase = 0;
    uint64_t byteBase = 0;
    bool repeat = false;
    uint64_t repetition = 0;
    uint64_t repetitions = 0;
    int64_t bitStride = 0;
    uint64_t byteStride = 0;
  };
  std::vector<Frame> stack;
  stack.push_back({records, recordCount, 0, bitBase, byteBase});
  while (!stack.empty()) {
    Frame &frame = stack.back();
    if (frame.repeat) {
      if (frame.repetition == frame.repetitions) {
        stack.pop_back();
        continue;
      }
      uint64_t repeatedBit = 0, repeatedByte = 0;
      uint64_t repetition = frame.repetition++;
      if (!addSignedMultiply(frame.bitBase, repetition, frame.bitStride,
                             repeatedBit) ||
          !addMultiply(frame.byteBase, repetition, frame.byteStride,
                       repeatedByte))
        return false;
      stack.push_back({frame.records, frame.recordCount, 0, repeatedBit,
                       repeatedByte});
      continue;
    }
    if (frame.index == frame.recordCount) {
      stack.pop_back();
      continue;
    }
    const int64_t *record = frame.records + frame.index * recordWords;
    if (record[0] == 0) {
      if (record[1] < 0 || record[2] < 0 || record[3] < 0 || record[3] > 11 ||
          record[4] <= 0 || record[5] < 0 || record[5] > 1 || record[6] < 0 ||
          record[6] > 1 || record[7] != 0)
        return false;
      uint64_t leafBit = 0, leafByte = 0;
      if (!addMultiply(frame.bitBase, 1,
                       static_cast<uint64_t>(record[1]), leafBit) ||
          !addMultiply(frame.byteBase, 1, static_cast<uint64_t>(record[2]),
                       leafByte) ||
          !visit(leafBit, leafByte, static_cast<uint32_t>(record[3]),
                 static_cast<uint32_t>(record[4]),
                 static_cast<uint32_t>(record[5])))
        return false;
      ++frame.index;
      continue;
    }
    if (record[0] != 1 || record[1] < 0 || record[2] < 0 || record[3] <= 0 ||
        record[4] == 0 || record[5] <= 0 || record[6] != 0 || record[7] <= 0)
      return false;
    uint64_t bodyRecords = static_cast<uint64_t>(record[7]);
    if (bodyRecords > frame.recordCount - frame.index - 1)
      return false;
    uint64_t repeatBitBase = 0, repeatByteBase = 0;
    if (!addMultiply(frame.bitBase, 1, static_cast<uint64_t>(record[1]),
                     repeatBitBase) ||
        !addMultiply(frame.byteBase, 1, static_cast<uint64_t>(record[2]),
                     repeatByteBase))
      return false;
    frame.index += bodyRecords + 1;
    stack.push_back({record + recordWords,
                     bodyRecords,
                     0,
                     repeatBitBase,
                     repeatByteBase,
                     true,
                     0,
                     static_cast<uint64_t>(record[3]),
                     record[4],
                     static_cast<uint64_t>(record[5])});
  }
  return true;
}

bool bitAt(const uint8_t *data, uint64_t bit) {
  return (data[bit / 8] >> (bit & 7)) & 1;
}

void setBit(uint8_t *data, uint64_t bit, bool value) {
  uint8_t mask = static_cast<uint8_t>(uint8_t{1} << (bit & 7));
  if (value)
    data[bit / 8] |= mask;
  else
    data[bit / 8] &= static_cast<uint8_t>(~mask);
}

uint64_t cLeafSize(uint32_t category, uint32_t width) {
  switch (category) {
  case 0:
  case 1:
    return width == 1 ? 1 : 0;
  case 2:
    return width == 8 ? 1 : 0;
  case 3:
    return width == 16 ? 2 : 0;
  case 4:
  case 10:
    return width == 32 ? 4 : 0;
  case 5:
  case 8:
  case 9:
  case 11:
    return width == 64 ? 8 : 0;
  case 6:
    return ((uint64_t{width} + 31) / 32) * 4;
  case 7:
    return ((uint64_t{width} + 31) / 32) * 8;
  default:
    return 0;
  }
}

bool validLeaf(uint64_t bit, uint64_t byte, uint32_t category, uint32_t width,
               uint32_t leafFourState, uint64_t totalWidth, uint64_t cSize) {
  uint64_t size = cLeafSize(category, width);
  if (size == 0 || bit > totalWidth || width > totalWidth - bit ||
      byte > cSize || size > cSize - byte)
    return false;
  return ((category == 1 || category == 7) ? 1u : 0u) == leafFourState;
}

void copyPlaneBits(const uint8_t *source, uint64_t sourceBit,
                   uint8_t *destination, uint64_t destinationBit,
                   uint32_t width) {
  if (((sourceBit | destinationBit) & 7) == 0) {
    uint32_t bytes = width / 8;
    if (bytes != 0)
      std::memcpy(destination + destinationBit / 8, source + sourceBit / 8,
                  bytes);
    sourceBit += uint64_t{bytes} * 8;
    destinationBit += uint64_t{bytes} * 8;
    width -= bytes * 8;
  }
  for (uint32_t bit = 0; bit != width; ++bit)
    setBit(destination, destinationBit + bit, bitAt(source, sourceBit + bit));
}

} // namespace

namespace {
thread_local std::vector<std::vector<std::string>> aggregateExportStrings;
} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_aggregate_pack(
    const void *value, const void *unknown, uint64_t planeSize,
    uint64_t totalBitWidth, uint32_t fourState, void *outData, uint64_t cSize,
    uint64_t outCapacity, const int64_t *plan, uint64_t planWords) {
  if (!value || !outData || !plan || planWords == 0 ||
      planWords % recordWords != 0 || fourState > 1 ||
      (fourState && !unknown) || planeSize < (totalBitWidth + 7) / 8 ||
      planeSize > SIZE_MAX || cSize == 0 || cSize > outCapacity ||
      outCapacity > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto *output = static_cast<uint8_t *>(outData);
  const auto *values = static_cast<const uint8_t *>(value);
  const auto *unknowns = static_cast<const uint8_t *>(unknown);
  std::memset(output, 0, static_cast<size_t>(outCapacity));
  uint64_t stringIndex = 0;
  obelisk_rt_status status = OBELISK_RT_OK;
  auto packLeaf = [&](uint64_t bit, uint64_t byte, uint32_t category,
                      uint32_t width, uint32_t leafFourState) {
    if (status != OBELISK_RT_OK ||
        !validLeaf(bit, byte, category, width, leafFourState, totalBitWidth,
                   cSize))
      return false;
    uint8_t *destination = output + byte;
    if (category == 0 || category == 1) {
      bool b = leafFourState && bitAt(unknowns, bit);
      bool a = bitAt(values, bit) != b;
      destination[0] = static_cast<uint8_t>(a | (b << 1));
      return true;
    }
    if (category == 8) {
      if (stringIndex >= (outCapacity - cSize) / 8)
        return false;
      uint64_t word = 0;
      copyPlaneBits(values, bit, reinterpret_cast<uint8_t *>(&word), 0, 64);
      char *scratch =
          reinterpret_cast<char *>(output + cSize + stringIndex * 8);
      const char *bytes = nullptr;
      uint64_t size = 0;
      status = obelisk_rt_v1_string_view(word, scratch, &bytes, &size);
      if (status != OBELISK_RT_OK)
        return false;
      std::memcpy(destination, &bytes, sizeof(bytes));
      ++stringIndex;
      return true;
    }
    if (category == 9) {
      uint64_t word = 0;
      copyPlaneBits(values, bit, reinterpret_cast<uint8_t *>(&word), 0, 64);
      void *pointer = reinterpret_cast<void *>(static_cast<uintptr_t>(word));
      std::memcpy(destination, &pointer, sizeof(pointer));
      return true;
    }
    if (category == 6 || category == 7) {
      uint64_t words = (uint64_t{width} + 31) / 32;
      for (uint64_t word = 0; word != words; ++word) {
        uint32_t count = static_cast<uint32_t>(
            std::min<uint64_t>(32, uint64_t{width} - word * 32));
        uint64_t source = bit + word * 32;
        uint32_t value = 0, bval = 0;
        copyPlaneBits(values, source, reinterpret_cast<uint8_t *>(&value), 0,
                      count);
        if (leafFourState)
          copyPlaneBits(unknowns, source, reinterpret_cast<uint8_t *>(&bval), 0,
                        count);
        uint32_t aval = value ^ bval;
        std::memcpy(destination + word * (leafFourState ? 8 : 4), &aval, 4);
        if (leafFourState)
          std::memcpy(destination + word * 8 + 4, &bval, 4);
      }
      return true;
    }
    copyPlaneBits(values, bit, destination, 0, width);
    return true;
  };
  if (!walkPlan(plan, planWords / recordWords, 0, 0, packLeaf))
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_ARGUMENT : status;
  return status;
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_aggregate_unpack(
    obelisk_rt_context *context, const void *data, uint64_t cSize,
    const int64_t *plan, uint64_t planWords, void *outValue, void *outUnknown,
    uint64_t planeSize, uint64_t totalBitWidth, uint32_t fourState) {
  if (!data || !plan || !outValue || planWords == 0 ||
      planWords % recordWords != 0 || fourState > 1 ||
      (fourState && !outUnknown) || planeSize < (totalBitWidth + 7) / 8 ||
      planeSize > SIZE_MAX || cSize == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto *values = static_cast<uint8_t *>(outValue);
  auto *unknowns = static_cast<uint8_t *>(outUnknown);
  const auto *source = static_cast<const uint8_t *>(data);
  std::memset(values, 0, static_cast<size_t>(planeSize));
  if (fourState)
    std::memset(unknowns, 0, static_cast<size_t>(planeSize));

  uint64_t stringCount = 0;
  auto countStrings = [&](uint64_t bit, uint64_t byte, uint32_t category,
                          uint32_t width, uint32_t leafFourState) {
    if (!validLeaf(bit, byte, category, width, leafFourState, totalBitWidth,
                   cSize))
      return false;
    if (category == 8) {
      if (stringCount == UINT64_MAX)
        return false;
      ++stringCount;
    }
    return true;
  };
  if (!walkPlan(plan, planWords / recordWords, 0, 0, countStrings) ||
      stringCount > SIZE_MAX / sizeof(obelisk_rt_managed_word_v1))
    return OBELISK_RT_INVALID_ARGUMENT;

  std::vector<obelisk_rt_managed_word_v1> stringRoots(
      static_cast<size_t>(stringCount), 0);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  obelisk_rt_gc_managed_root_range_v1 roots{};
  if (stringCount != 0) {
    if (!context || !(lane = obelisk_rt_v1_gc_current_lane(context)))
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_status push = obelisk_rt_v1_gc_managed_root_range_push(
        lane, &roots, stringRoots.data(), stringCount);
    if (push != OBELISK_RT_OK)
      return push;
  }
  uint64_t stringIndex = 0;
  obelisk_rt_status status = OBELISK_RT_OK;
  auto unpackLeaf = [&](uint64_t bit, uint64_t byte, uint32_t category,
                        uint32_t width, uint32_t leafFourState) {
    if (status != OBELISK_RT_OK ||
        !validLeaf(bit, byte, category, width, leafFourState, totalBitWidth,
                   cSize))
      return false;
    const uint8_t *current = source + byte;
    if (category == 0 || category == 1) {
      bool a = current[0] & 1;
      bool b = (current[0] >> 1) & 1;
      setBit(values, bit, a != b);
      if (leafFourState)
        setBit(unknowns, bit, b);
      return true;
    }
    if (category == 8) {
      const char *bytes = nullptr;
      std::memcpy(&bytes, current, sizeof(bytes));
      status = obelisk_rt_v1_dpi_string_copy(context, bytes,
                                             &stringRoots[stringIndex]);
      if (status != OBELISK_RT_OK)
        return false;
      copyPlaneBits(
          reinterpret_cast<const uint8_t *>(&stringRoots[stringIndex]), 0,
          values, bit, 64);
      ++stringIndex;
      return true;
    }
    if (category == 9) {
      void *pointer = nullptr;
      std::memcpy(&pointer, current, sizeof(pointer));
      uint64_t word =
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
      copyPlaneBits(reinterpret_cast<const uint8_t *>(&word), 0, values, bit,
                    64);
      return true;
    }
    if (category == 6 || category == 7) {
      uint64_t words = (uint64_t{width} + 31) / 32;
      for (uint64_t word = 0; word != words; ++word) {
        uint32_t aval = 0, bval = 0;
        std::memcpy(&aval, current + word * (leafFourState ? 8 : 4), 4);
        if (leafFourState)
          std::memcpy(&bval, current + word * 8 + 4, 4);
        uint32_t count = static_cast<uint32_t>(
            std::min<uint64_t>(32, uint64_t{width} - word * 32));
        uint32_t value = aval ^ bval;
        uint64_t destination = bit + word * 32;
        copyPlaneBits(reinterpret_cast<const uint8_t *>(&value), 0, values,
                      destination, count);
        if (leafFourState)
          copyPlaneBits(reinterpret_cast<const uint8_t *>(&bval), 0, unknowns,
                        destination, count);
      }
      return true;
    }
    copyPlaneBits(current, 0, values, bit, width);
    return true;
  };
  bool walked = walkPlan(plan, planWords / recordWords, 0, 0, unpackLeaf);
  if (stringCount != 0) {
    obelisk_rt_status pop =
        obelisk_rt_v1_gc_managed_root_range_pop(lane, &roots);
    if (status == OBELISK_RT_OK && pop != OBELISK_RT_OK)
      status = pop;
  }
  if (!walked && status == OBELISK_RT_OK)
    status = OBELISK_RT_INVALID_ARGUMENT;
  return status;
}

namespace {

struct AggregateStringRoots {
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ManagedRootProvider provider{};
  uint8_t *value = nullptr;
  uint64_t planeSize = 0;
  uint64_t totalBitWidth = 0;
  std::vector<int64_t> plan;
};

void enumerateAggregateStringRoots(void *opaque, ManagedRootVisit visit,
                                   void *visitorEnvironment) {
  auto *roots = static_cast<AggregateStringRoots *>(opaque);
  if (!roots || !visit)
    return;
  auto enumerate = [&](uint64_t bit, uint64_t, uint32_t category,
                       uint32_t width, uint32_t) {
    if (category != 8)
      return true;
    if (width != 64 || bit % 64 != 0 || bit > roots->totalBitWidth ||
        64 > roots->totalBitWidth - bit || bit / 8 > roots->planeSize ||
        sizeof(obelisk_rt_managed_word_v1) > roots->planeSize - bit / 8)
      return false;
    auto *slot = reinterpret_cast<obelisk_rt_managed_word_v1 *>(
        roots->value + bit / 8);
    // Inline strings are immediate values. Heap strings use an aligned object
    // pointer word and can be visited in place so a moving collector updates
    // the aggregate plane before dispatch or copy-out resumes.
    if (*slot != 0 && (*slot & 3) == 0)
      visit(visitorEnvironment,
            reinterpret_cast<obelisk_rt_object_v1 **>(slot));
    return true;
  };
  (void)walkPlan(roots->plan.data(), roots->plan.size() / recordWords, 0, 0,
                 enumerate);
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_aggregate_roots_push(
    obelisk_rt_context *context, void *value, uint64_t planeSize,
    uint64_t totalBitWidth, const int64_t *plan, uint64_t planWords,
    void **outHandle) {
  if (!context || !value || !plan || planWords == 0 ||
      planWords % recordWords != 0 || planeSize < (totalBitWidth + 7) / 8 ||
      !outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = nullptr;
  auto validate = [&](uint64_t bit, uint64_t, uint32_t category,
                      uint32_t width, uint32_t) {
    return category != 8 ||
           (width == 64 && bit % 64 == 0 && bit <= totalBitWidth &&
            64 <= totalBitWidth - bit && bit / 8 <= planeSize &&
            sizeof(obelisk_rt_managed_word_v1) <= planeSize - bit / 8);
  };
  if (!walkPlan(plan, planWords / recordWords, 0, 0, validate))
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
  if (!lane)
    return OBELISK_RT_INVALID_LIFECYCLE;
  std::unique_ptr<AggregateStringRoots> roots;
  OBELISK_RT_TRY { roots = std::make_unique<AggregateStringRoots>(); }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  roots->lane = lane;
  roots->value = static_cast<uint8_t *>(value);
  roots->planeSize = planeSize;
  roots->totalBitWidth = totalBitWidth;
  OBELISK_RT_TRY { roots->plan.assign(plan, plan + planWords); }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  obelisk_rt_status status = obelisk_rt_managed_roots_push(
      lane, &roots->provider, enumerateAggregateStringRoots, roots.get());
  if (status != OBELISK_RT_OK)
    return status;
  *outHandle = roots.release();
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_aggregate_roots_pop(
    obelisk_rt_context *context, void *handle) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!handle)
    return OBELISK_RT_OK;
  auto *roots = static_cast<AggregateStringRoots *>(handle);
  if (obelisk_rt_managed_lane_context(roots->lane) != context)
    return OBELISK_RT_INVALID_HANDLE;
  obelisk_rt_status status =
      obelisk_rt_managed_roots_pop(roots->lane, &roots->provider);
  if (status != OBELISK_RT_OK)
    return status;
  delete roots;
  return OBELISK_RT_OK;
}

namespace {

bool makeOpenArrayPlan(uint32_t elementBitWidth, uint64_t elementCount,
                       uint32_t reverseElements, uint64_t elementCSize,
                       const int64_t *unpackedRanges,
                       uint32_t unpackedDimensions, const int64_t *elementPlan,
                       uint64_t elementPlanWords, std::vector<int64_t> &plan) {
  if (elementBitWidth == 0 || elementCount == 0 || reverseElements > 1 ||
      elementCSize == 0 || elementCSize > INT64_MAX || !elementPlan ||
      elementPlanWords == 0 || elementPlanWords % recordWords != 0 ||
      elementPlanWords / recordWords > INT64_MAX || elementCount > INT64_MAX ||
      (unpackedDimensions != 0 && !unpackedRanges) ||
      elementCount - 1 > UINT64_MAX / elementBitWidth)
    return false;
  plan.clear();
  if (unpackedDimensions == 0) {
    uint64_t lastBit = (elementCount - 1) * elementBitWidth;
    if (lastBit > INT64_MAX)
      return false;
    int64_t firstBit = reverseElements ? static_cast<int64_t>(lastBit) : 0;
    int64_t bitStride = reverseElements ? -static_cast<int64_t>(elementBitWidth)
                                        : static_cast<int64_t>(elementBitWidth);
    plan.insert(plan.end(),
                {1, firstBit, 0, static_cast<int64_t>(elementCount), bitStride,
                 static_cast<int64_t>(elementCSize), 0, 0});
  } else {
    if (reverseElements != 0)
      return false;
    uint64_t remaining = elementCount;
    for (uint32_t dimension = 0; dimension != unpackedDimensions; ++dimension) {
      int64_t left = unpackedRanges[uint64_t{dimension} * 2];
      int64_t right = unpackedRanges[uint64_t{dimension} * 2 + 1];
      uint64_t extent =
          left >= right
              ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right) + 1
              : static_cast<uint64_t>(right) - static_cast<uint64_t>(left) + 1;
      if (extent == 0 || remaining % extent != 0)
        return false;
      uint64_t innerElements = remaining / extent;
      if (innerElements > UINT64_MAX / elementBitWidth ||
          innerElements > UINT64_MAX / elementCSize)
        return false;
      uint64_t bitStride = innerElements * elementBitWidth;
      uint64_t byteStride = innerElements * elementCSize;
      uint64_t firstBit = left > right ? 0 : (extent - 1) * bitStride;
      if (bitStride > INT64_MAX || byteStride > INT64_MAX ||
          firstBit > INT64_MAX || extent > INT64_MAX)
        return false;
      plan.insert(plan.end(), {1, static_cast<int64_t>(firstBit), 0,
                               static_cast<int64_t>(extent),
                               left > right ? static_cast<int64_t>(bitStride)
                                            : -static_cast<int64_t>(bitStride),
                               static_cast<int64_t>(byteStride), 0, 0});
      remaining = innerElements;
    }
    if (remaining != 1)
      return false;
  }
  plan.insert(plan.end(), elementPlan, elementPlan + elementPlanWords);
  uint64_t prefixRecords = unpackedDimensions == 0 ? 1 : unpackedDimensions;
  for (uint64_t record = 0; record != prefixRecords; ++record) {
    uint64_t bodyRecords = plan.size() / recordWords - record - 1;
    if (bodyRecords == 0 || bodyRecords > INT64_MAX)
      return false;
    plan[record * recordWords + 7] = static_cast<int64_t>(bodyRecords);
  }
  return true;
}

} // namespace

extern "C" obelisk_rt_status
obelisk_rt_v1_dpi_open_array_aggregate_roots_push(
    obelisk_rt_context *context, void *value, uint64_t planeSize,
    uint64_t totalBitWidth, uint32_t elementBitWidth, uint64_t elementCount,
    uint32_t reverseElements, const int64_t *unpackedRanges,
    uint32_t unpackedDimensions, uint64_t elementCSize,
    const int64_t *elementPlan, uint64_t elementPlanWords, void **outHandle) {
  if (!outHandle)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outHandle = nullptr;
  if (elementCount == 0)
    return totalBitWidth == 0 ? OBELISK_RT_OK
                              : OBELISK_RT_INVALID_ARGUMENT;
  std::vector<int64_t> plan;
  OBELISK_RT_TRY {
    if (!makeOpenArrayPlan(elementBitWidth, elementCount, reverseElements,
                           elementCSize, unpackedRanges, unpackedDimensions,
                           elementPlan, elementPlanWords, plan))
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  return obelisk_rt_v1_dpi_aggregate_roots_push(
      context, value, planeSize, totalBitWidth, plan.data(), plan.size(),
      outHandle);
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_open_array_aggregate_pack(
    const void *value, const void *unknown, uint64_t planeSize,
    uint64_t totalBitWidth, uint32_t fourState, uint32_t elementBitWidth,
    uint64_t elementCount, uint32_t reverseElements,
    const int64_t *unpackedRanges, uint32_t unpackedDimensions, void *outData,
    uint64_t dataSize, uint64_t outCapacity, uint64_t elementCSize,
    uint64_t elementStringCount, const int64_t *elementPlan,
    uint64_t elementPlanWords) {
  if (elementCount == 0)
    return totalBitWidth == 0 && dataSize == 0 && outCapacity == 0
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_ARGUMENT;
  if (elementCount > UINT64_MAX / elementBitWidth ||
      elementCount * elementBitWidth != totalBitWidth ||
      elementCount > UINT64_MAX / elementCSize ||
      elementCount * elementCSize != dataSize ||
      (elementStringCount != 0 &&
       elementCount > UINT64_MAX / elementStringCount))
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t strings = elementStringCount * elementCount;
  if (strings > (UINT64_MAX - dataSize) / 8 ||
      dataSize + strings * 8 != outCapacity)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::vector<int64_t> plan;
  if (!makeOpenArrayPlan(elementBitWidth, elementCount, reverseElements,
                         elementCSize, unpackedRanges, unpackedDimensions,
                         elementPlan, elementPlanWords, plan))
    return OBELISK_RT_INVALID_ARGUMENT;
  return obelisk_rt_v1_dpi_aggregate_pack(
      value, unknown, planeSize, totalBitWidth, fourState, outData, dataSize,
      outCapacity, plan.data(), plan.size());
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_open_array_aggregate_unpack(
    obelisk_rt_context *context, const void *data, uint64_t dataSize,
    uint64_t elementCSize, const int64_t *elementPlan,
    uint64_t elementPlanWords, uint64_t elementCount, uint32_t reverseElements,
    const int64_t *unpackedRanges, uint32_t unpackedDimensions, void *outValue,
    void *outUnknown, uint64_t planeSize, uint64_t totalBitWidth,
    uint32_t fourState, uint32_t elementBitWidth) {
  if (elementCount == 0)
    return totalBitWidth == 0 && dataSize == 0 ? OBELISK_RT_OK
                                               : OBELISK_RT_INVALID_ARGUMENT;
  if (elementCount > UINT64_MAX / elementBitWidth ||
      elementCount * elementBitWidth != totalBitWidth ||
      elementCount > UINT64_MAX / elementCSize ||
      elementCount * elementCSize != dataSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::vector<int64_t> plan;
  if (!makeOpenArrayPlan(elementBitWidth, elementCount, reverseElements,
                         elementCSize, unpackedRanges, unpackedDimensions,
                         elementPlan, elementPlanWords, plan))
    return OBELISK_RT_INVALID_ARGUMENT;
  return obelisk_rt_v1_dpi_aggregate_unpack(
      context, data, dataSize, plan.data(), plan.size(), outValue, outUnknown,
      planeSize, totalBitWidth, fourState);
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_aggregate_export_pack(
    const void *value, const void *unknown, uint64_t planeSize,
    uint64_t totalBitWidth, uint32_t fourState, void *outData, uint64_t cSize,
    const int64_t *plan, uint64_t planWords, uint32_t outputSlot) {
  if (!outData || !plan || planWords == 0 || planWords % recordWords != 0 ||
      cSize > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t stringCount = 0;
  auto count = [&](uint64_t, uint64_t, uint32_t category, uint32_t, uint32_t) {
    if (category == 8) {
      if (stringCount == UINT64_MAX)
        return false;
      ++stringCount;
    }
    return true;
  };
  if (!walkPlan(plan, planWords / recordWords, 0, 0, count) ||
      stringCount > (UINT64_MAX - cSize) / 8 ||
      cSize + stringCount * 8 > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t capacity = cSize + stringCount * 8;
  std::vector<uint8_t> temporary(static_cast<size_t>(capacity));
  obelisk_rt_status status = obelisk_rt_v1_dpi_aggregate_pack(
      value, unknown, planeSize, totalBitWidth, fourState, temporary.data(),
      cSize, capacity, plan, planWords);
  if (status != OBELISK_RT_OK)
    return status;
  if (aggregateExportStrings.size() <= outputSlot)
    aggregateExportStrings.resize(static_cast<size_t>(outputSlot) + 1);
  std::vector<std::string> &strings = aggregateExportStrings[outputSlot];
  strings.clear();
  strings.reserve(static_cast<size_t>(stringCount));
  auto retain = [&](uint64_t, uint64_t byte, uint32_t category, uint32_t,
                    uint32_t) {
    if (category != 8)
      return true;
    if (byte > cSize || sizeof(const char *) > cSize - byte)
      return false;
    const char *text = nullptr;
    std::memcpy(&text, temporary.data() + byte, sizeof(text));
    if (!text)
      return false;
    strings.emplace_back(text);
    text = strings.back().c_str();
    std::memcpy(temporary.data() + byte, &text, sizeof(text));
    return true;
  };
  if (!walkPlan(plan, planWords / recordWords, 0, 0, retain))
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memcpy(outData, temporary.data(), static_cast<size_t>(cSize));
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_aggregate_state_alloc(
    obelisk_rt_context *context, uint64_t totalBitWidth, const uint8_t *value,
    const uint8_t *unknown, const int64_t *plan, uint64_t planWords,
    uint64_t *outHandle) {
  if (!context || !value || !plan || !outHandle || totalBitWidth == 0 ||
      planWords == 0 || planWords % recordWords != 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::vector<obelisk_rt_managed_root_slot_v1> slots;
  auto collect = [&](uint64_t bit, uint64_t, uint32_t category, uint32_t width,
                     uint32_t) {
    if (bit > totalBitWidth || width > totalBitWidth - bit)
      return false;
    if (category != 8)
      return true;
    if (width != 64 || (bit & 63) != 0)
      return false;
    slots.push_back({bit, OBELISK_RT_MANAGED_ROOT_KIND_STRING, 0});
    return true;
  };
  if (!walkPlan(plan, planWords / recordWords, 0, 0, collect) ||
      slots.empty())
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!std::is_sorted(slots.begin(), slots.end(),
                      [](const auto &lhs, const auto &rhs) {
                        return lhs.bit_offset < rhs.bit_offset;
                      }))
    std::sort(slots.begin(), slots.end(), [](const auto &lhs, const auto &rhs) {
      return lhs.bit_offset < rhs.bit_offset;
    });
  if (std::adjacent_find(slots.begin(), slots.end(),
                         [](const auto &lhs, const auto &rhs) {
                           return lhs.bit_offset == rhs.bit_offset;
                         }) != slots.end())
    return OBELISK_RT_INVALID_ARGUMENT;
  return obelisk_rt_v1_native_state_alloc_with_typed_roots(
      context, totalBitWidth, value, unknown, slots.data(), slots.size(),
      outHandle);
}
