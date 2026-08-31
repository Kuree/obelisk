//===- DPIOpenArray.cpp - IEEE DPI open-array access layer --------------===//

#include "RuntimeInternal.h"
#include "obelisk/Runtime/Runtime.h"
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
#define DPI_DLLISPEC __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define DPI_EXTERN __attribute__((visibility("default")))
#endif
#include "svdpi.h"

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

extern decltype(&obelisk_rt_v1_dpi_open_array_aggregate_pack)
    designBytecodeDpiOpenAggregatePack;
extern decltype(&obelisk_rt_v1_dpi_open_array_aggregate_unpack)
    designBytecodeDpiOpenAggregateUnpack;
extern decltype(&obelisk_rt_v1_dpi_aggregate_pack)
    designBytecodeDpiAggregatePack;
extern decltype(&obelisk_rt_v1_dpi_aggregate_unpack)
    designBytecodeDpiAggregateUnpack;
extern decltype(&obelisk_rt_v1_dpi_open_array_aggregate_roots_push)
    designBytecodeDpiOpenAggregateRootsPush;
extern decltype(&obelisk_rt_v1_dpi_aggregate_roots_pop)
    designBytecodeDpiAggregateRootsPop;
extern decltype(&obelisk_rt_v1_dpi_open_array_prepare_recursive)
    designBytecodeDpiOpenPrepareRecursive;
extern decltype(&obelisk_rt_v1_dpi_open_array_finish_recursive)
    designBytecodeDpiOpenFinishRecursive;
extern decltype(&obelisk_rt_v1_dpi_aggregate_state_alloc)
    designBytecodeDpiAggregateStateAlloc;

namespace {

uint64_t canonicalElementSize(uint32_t category, uint32_t width) {
  switch (category) {
  case 0: // bit
  case 1: // logic
  case 2: // byte
    return 1;
  case 3: // shortint
    return 2;
  case 4:  // int
  case 10: // shortreal
    return 4;
  case 5:  // longint
  case 11: // real
    return 8;
  case 6: // packed bit
    return ((uint64_t{width} + 31) / 32) * sizeof(svBitVecVal);
  case 7: // packed logic
    return ((uint64_t{width} + 31) / 32) * sizeof(svLogicVecVal);
  default:
    return 0;
  }
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

bool validCanonicalTransfer(uint64_t planeSize, uint64_t totalWidth,
                            uint32_t fourState, uint32_t category,
                            uint32_t elementWidth, uint64_t elementCount,
                            uint64_t dataSize, uint64_t &elementSize) {
  elementSize = canonicalElementSize(category, elementWidth);
  if (fourState > 1 || elementWidth == 0 || elementSize == 0 ||
      elementCount > UINT64_MAX / elementWidth ||
      elementCount * elementWidth != totalWidth ||
      planeSize < (totalWidth + 7) / 8 ||
      elementCount > UINT64_MAX / elementSize ||
      elementCount * elementSize != dataSize)
    return false;
  bool categoryFourState = category == 1 || category == 7;
  return categoryFourState == (fourState != 0);
}

const obelisk_rt_dpi_open_array_v1 *descriptor(svOpenArrayHandle handle) {
  auto *value = static_cast<const obelisk_rt_dpi_open_array_v1 *>(handle);
  if (!value || value->magic != OBELISK_RT_DPI_OPEN_ARRAY_MAGIC ||
      value->reserved != 0 || (value->dimensions != 0 && !value->ranges) ||
      (value->data_size != 0 && !value->data) || value->element_size == 0)
    return nullptr;
  for (uint32_t dimension = 0; dimension != value->dimensions; ++dimension)
    if (value->ranges[dimension].reserved != 0 ||
        (value->ranges[dimension].flags &
         ~(OBELISK_RT_DPI_DIMENSION_EMPTY |
           OBELISK_RT_DPI_DIMENSION_RUNTIME)) != 0)
      return nullptr;
  return value;
}

const obelisk_rt_dpi_dimension_v1 *
dimensionRecord(const obelisk_rt_dpi_open_array_v1 &array, int dimension) {
  return dimension >= 1 && static_cast<uint32_t>(dimension) <= array.dimensions
             ? &array.ranges[dimension - 1]
             : nullptr;
}

bool range(const obelisk_rt_dpi_open_array_v1 &array, int dimension,
           int32_t &left, int32_t &right) {
  if (dimension == 0) {
    if ((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_PACKED) == 0)
      return false;
    left = array.packed_left;
    right = array.packed_right;
    return true;
  }
  if (dimension < 1 || static_cast<uint32_t>(dimension) > array.dimensions)
    return false;
  const obelisk_rt_dpi_dimension_v1 &value = array.ranges[dimension - 1];
  left = value.left;
  right = value.right;
  return true;
}

bool extent(int32_t left, int32_t right, uint64_t &result) {
  int64_t distance = left >= right ? int64_t{left} - int64_t{right}
                                   : int64_t{right} - int64_t{left};
  result = static_cast<uint64_t>(distance) + 1;
  return true;
}

bool addIndex(const obelisk_rt_dpi_dimension_v1 &dimension, int index,
              uint64_t &offset) {
  int64_t position =
      int64_t{index} - std::min<int64_t>(dimension.left, dimension.right);
  uint64_t count = 0;
  extent(dimension.left, dimension.right, count);
  if (position < 0 || static_cast<uint64_t>(position) >= count)
    return false;
  uint64_t unsignedPosition = static_cast<uint64_t>(position);
  if (unsignedPosition != 0 &&
      dimension.byte_stride >
          std::numeric_limits<uint64_t>::max() / unsignedPosition)
    return false;
  uint64_t contribution = unsignedPosition * dimension.byte_stride;
  if (contribution > std::numeric_limits<uint64_t>::max() - offset)
    return false;
  offset += contribution;
  return true;
}

void *elementPointer(const obelisk_rt_dpi_open_array_v1 &array,
                     uint32_t indexCount, int first, va_list arguments) {
  if (indexCount != array.dimensions || indexCount == 0 || !array.data)
    return nullptr;
  uint64_t offset = 0;
  for (uint32_t dimension = 0; dimension != indexCount; ++dimension) {
    int index = dimension == 0 ? first : va_arg(arguments, int);
    if (!addIndex(array.ranges[dimension], index, offset))
      return nullptr;
  }
  if (offset > array.data_size || array.element_size > array.data_size - offset)
    return nullptr;
  return static_cast<unsigned char *>(array.data) + offset;
}

void *elementPointer(const obelisk_rt_dpi_open_array_v1 &array,
                     uint32_t indexCount, const int *indices) {
  if (indexCount != array.dimensions || indexCount == 0 || !array.data)
    return nullptr;
  uint64_t offset = 0;
  for (uint32_t dimension = 0; dimension != indexCount; ++dimension)
    if (!addIndex(array.ranges[dimension], indices[dimension], offset))
      return nullptr;
  if (offset > array.data_size || array.element_size > array.data_size - offset)
    return nullptr;
  return static_cast<unsigned char *>(array.data) + offset;
}

uint64_t canonicalSize(const obelisk_rt_dpi_open_array_v1 &array, bool logic) {
  if ((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_PACKED) == 0 ||
      array.element_bit_width == 0 ||
      (((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE) != 0) != logic))
    return 0;
  uint64_t words = (uint64_t{array.element_bit_width} + 31) / 32;
  return words * (logic ? sizeof(svLogicVecVal) : sizeof(svBitVecVal));
}

void copyVector(void *destination, const void *source,
                const obelisk_rt_dpi_open_array_v1 &array, bool logic) {
  uint64_t size = canonicalSize(array, logic);
  if (!destination || !source || size == 0 || size > array.element_size ||
      size > std::numeric_limits<size_t>::max())
    return;
  std::memcpy(destination, source, static_cast<size_t>(size));
  unsigned tail = array.element_bit_width % 32;
  if (tail == 0)
    return;
  uint32_t mask = (uint32_t{1} << tail) - 1;
  uint64_t last = (uint64_t{array.element_bit_width} + 31) / 32 - 1;
  if (logic) {
    auto *words = static_cast<svLogicVecVal *>(destination);
    words[last].aval &= mask;
    words[last].bval &= mask;
  } else {
    static_cast<svBitVecVal *>(destination)[last] &= mask;
  }
}

template <typename Callable>
void withVariadicElement(svOpenArrayHandle handle, int first, va_list arguments,
                         Callable &&callable) {
  const auto *array = descriptor(handle);
  if (!array)
    return;
  void *element = elementPointer(*array, array->dimensions, first, arguments);
  if (element)
    callable(*array, element);
}

template <typename Callable>
void withFixedElement(svOpenArrayHandle handle, uint32_t count,
                      const int *indices, Callable &&callable) {
  const auto *array = descriptor(handle);
  if (!array)
    return;
  void *element = elementPointer(*array, count, indices);
  if (element)
    callable(*array, element);
}

} // namespace

// The always-live bytecode dispatcher references the DPI marshallers weakly.
// A design containing a bytecode DPI import emits one strong call to this
// anchor, which pulls both the standardized open-array API and the recursive
// aggregate marshaller into that executable only.
extern "C" void obelisk_rt_v1_dpi_import_bytecode_link_anchor(void) {
  designBytecodeDpiOpenAggregatePack =
      &obelisk_rt_v1_dpi_open_array_aggregate_pack;
  designBytecodeDpiOpenAggregateUnpack =
      &obelisk_rt_v1_dpi_open_array_aggregate_unpack;
  designBytecodeDpiAggregatePack = &obelisk_rt_v1_dpi_aggregate_pack;
  designBytecodeDpiAggregateUnpack = &obelisk_rt_v1_dpi_aggregate_unpack;
  designBytecodeDpiOpenAggregateRootsPush =
      &obelisk_rt_v1_dpi_open_array_aggregate_roots_push;
  designBytecodeDpiAggregateRootsPop = &obelisk_rt_v1_dpi_aggregate_roots_pop;
  designBytecodeDpiOpenPrepareRecursive =
      &obelisk_rt_v1_dpi_open_array_prepare_recursive;
  designBytecodeDpiOpenFinishRecursive =
      &obelisk_rt_v1_dpi_open_array_finish_recursive;
  designBytecodeDpiAggregateStateAlloc =
      &obelisk_rt_v1_dpi_aggregate_state_alloc;
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_open_array_pack(
    const void *value, const void *unknown, uint64_t planeSize,
    uint64_t totalBitWidth, uint32_t fourState, uint32_t elementCategory,
    uint32_t elementBitWidth, uint64_t elementCount, uint32_t reverseElements,
    void *outData, uint64_t outSize) {
  uint64_t elementSize = 0;
  if (elementCount == 0)
    return validCanonicalTransfer(planeSize, totalBitWidth, fourState,
                                  elementCategory, elementBitWidth,
                                  elementCount, outSize, elementSize)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_ARGUMENT;
  if (!value || !outData || (fourState && !unknown) || reverseElements > 1 ||
      !validCanonicalTransfer(planeSize, totalBitWidth, fourState,
                              elementCategory, elementBitWidth, elementCount,
                              outSize, elementSize) ||
      planeSize > SIZE_MAX || outSize > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto *destination = static_cast<uint8_t *>(outData);
  const auto *values = static_cast<const uint8_t *>(value);
  const auto *unknowns = static_cast<const uint8_t *>(unknown);
  std::memset(destination, 0, static_cast<size_t>(outSize));
  for (uint64_t element = 0; element != elementCount; ++element) {
    uint8_t *current = destination + element * elementSize;
    uint64_t sourceElement =
        reverseElements ? elementCount - 1 - element : element;
    uint64_t sourceBase = sourceElement * elementBitWidth;
    if (elementCategory == 0 || elementCategory == 1) {
      bool b = fourState && bitAt(unknowns, sourceBase);
      bool a = bitAt(values, sourceBase) != b;
      current[0] = static_cast<uint8_t>(a | (b << 1));
      continue;
    }
    if (elementCategory >= 2 && elementCategory <= 5) {
      for (uint32_t bit = 0; bit != elementBitWidth; ++bit)
        setBit(current, bit, bitAt(values, sourceBase + bit));
      continue;
    }
    if (elementCategory == 10 || elementCategory == 11) {
      for (uint32_t bit = 0; bit != elementBitWidth; ++bit)
        setBit(current, bit, bitAt(values, sourceBase + bit));
      continue;
    }
    uint64_t words = (uint64_t{elementBitWidth} + 31) / 32;
    for (uint64_t word = 0; word != words; ++word) {
      uint32_t aval = 0;
      uint32_t bval = 0;
      uint32_t count = static_cast<uint32_t>(
          std::min<uint64_t>(32, elementBitWidth - word * 32));
      for (uint32_t bit = 0; bit != count; ++bit) {
        uint64_t source = sourceBase + word * 32 + bit;
        bool b = fourState && bitAt(unknowns, source);
        bool a = bitAt(values, source) != b;
        aval |= static_cast<uint32_t>(a) << bit;
        bval |= static_cast<uint32_t>(b) << bit;
      }
      if (elementCategory == 6)
        std::memcpy(current + word * 4, &aval, sizeof(aval));
      else {
        std::memcpy(current + word * 8, &aval, sizeof(aval));
        std::memcpy(current + word * 8 + 4, &bval, sizeof(bval));
      }
    }
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_open_array_unpack(
    const void *data, uint64_t dataSize, uint32_t elementCategory,
    uint32_t elementBitWidth, uint64_t elementCount, void *outValue,
    void *outUnknown, uint64_t planeSize, uint64_t totalBitWidth,
    uint32_t fourState, uint32_t reverseElements) {
  uint64_t elementSize = 0;
  if (elementCount == 0)
    return validCanonicalTransfer(planeSize, totalBitWidth, fourState,
                                  elementCategory, elementBitWidth,
                                  elementCount, dataSize, elementSize)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_ARGUMENT;
  if (!data || !outValue || (fourState && !outUnknown) || reverseElements > 1 ||
      !validCanonicalTransfer(planeSize, totalBitWidth, fourState,
                              elementCategory, elementBitWidth, elementCount,
                              dataSize, elementSize) ||
      planeSize > SIZE_MAX || dataSize > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto *values = static_cast<uint8_t *>(outValue);
  auto *unknowns = static_cast<uint8_t *>(outUnknown);
  const auto *source = static_cast<const uint8_t *>(data);
  std::memset(values, 0, static_cast<size_t>(planeSize));
  if (fourState)
    std::memset(unknowns, 0, static_cast<size_t>(planeSize));
  for (uint64_t element = 0; element != elementCount; ++element) {
    const uint8_t *current = source + element * elementSize;
    uint64_t destinationElement =
        reverseElements ? elementCount - 1 - element : element;
    uint64_t destinationBase = destinationElement * elementBitWidth;
    if (elementCategory == 0 || elementCategory == 1) {
      bool a = current[0] & 1;
      bool b = (current[0] >> 1) & 1;
      setBit(values, destinationBase, a != b);
      if (fourState)
        setBit(unknowns, destinationBase, b);
      continue;
    }
    if ((elementCategory >= 2 && elementCategory <= 5) ||
        elementCategory == 10 || elementCategory == 11) {
      for (uint32_t bit = 0; bit != elementBitWidth; ++bit)
        setBit(values, destinationBase + bit, bitAt(current, bit));
      continue;
    }
    uint64_t words = (uint64_t{elementBitWidth} + 31) / 32;
    for (uint64_t word = 0; word != words; ++word) {
      uint32_t aval = 0;
      uint32_t bval = 0;
      std::memcpy(&aval, current + word * (fourState ? 8 : 4), sizeof(aval));
      if (fourState)
        std::memcpy(&bval, current + word * 8 + 4, sizeof(bval));
      uint32_t count = static_cast<uint32_t>(
          std::min<uint64_t>(32, elementBitWidth - word * 32));
      for (uint32_t bit = 0; bit != count; ++bit) {
        uint64_t destination = destinationBase + word * 32 + bit;
        bool b = (bval >> bit) & 1;
        bool a = (aval >> bit) & 1;
        setBit(values, destination, a != b);
        if (fourState)
          setBit(unknowns, destination, b);
      }
    }
  }
  return OBELISK_RT_OK;
}

extern "C" void obelisk_rt_v1_dpi_open_array_release_recursive(
    obelisk_rt_dpi_open_array_storage_v1 *storage) {
  if (!storage)
    return;
  std::free(storage->allocation);
  std::memset(storage, 0, sizeof(*storage));
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_open_array_prepare_recursive(
    const void *value, const void *unknown, uint64_t planeSize,
    uint64_t transportWidth, uint32_t transportFourState, uint32_t writable,
    uint32_t elementCategory, uint32_t elementWidth, uint32_t elementFourState,
    int32_t packedLeft, int32_t packedRight, uint64_t elementCSize,
    uint32_t elementCAlignment, uint64_t elementStringCount,
    const int64_t *elementPlan, uint64_t elementPlanWords,
    const int64_t *shapePlan, uint32_t dimensions,
    obelisk_rt_dpi_open_array_storage_v1 *outStorage) {
  constexpr uint64_t shapeWords = 8;
  if (!outStorage)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outStorage, 0, sizeof(*outStorage));
  if (!value || !shapePlan || !elementPlan || dimensions == 0 ||
      elementPlanWords == 0 || elementPlanWords % 8 != 0 ||
      transportWidth == 0 || elementWidth == 0 || transportFourState > 1 ||
      writable > 1 || elementFourState > 1 ||
      (transportFourState && !unknown) ||
      planeSize < (transportWidth + 7) / 8 || elementCSize == 0 ||
      elementCAlignment == 0 ||
      (elementCAlignment & (elementCAlignment - 1)) != 0 ||
      elementCAlignment > alignof(std::max_align_t) ||
      elementStringCount > (UINT64_MAX - elementCSize) / 8)
    return OBELISK_RT_INVALID_ARGUMENT;

  struct View {
    const uint8_t *value = nullptr;
    const uint8_t *unknown = nullptr;
    uint64_t planeSize = 0;
    uint64_t span = 0;
    uint64_t base = 0;
    bool fourState = false;
  };
  std::vector<obelisk_rt_dpi_dimension_v1> ranges(dimensions);
  std::vector<uint8_t> runtimeKnown(dimensions, 0);
  std::vector<uint8_t> flatValue;
  std::vector<uint8_t> flatUnknown;
  uint64_t elementCount = 0;
  obelisk_rt_status status = OBELISK_RT_OK;

  auto copyBits = [](uint8_t *destination, uint64_t destinationBit,
                     const uint8_t *source, uint64_t sourceBit,
                     uint64_t width) {
    if (((destinationBit | sourceBit | width) & 7) == 0) {
      std::memcpy(destination + destinationBit / 8, source + sourceBit / 8,
                  static_cast<size_t>(width / 8));
      return;
    }
    for (uint64_t bit = 0; bit != width; ++bit)
      setBit(destination, destinationBit + bit, bitAt(source, sourceBit + bit));
  };
  auto checkedExtent = [](int64_t left, int64_t right, uint64_t &result) {
    uint64_t distance =
        left >= right
            ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right)
            : static_cast<uint64_t>(right) - static_cast<uint64_t>(left);
    if (distance == UINT64_MAX)
      return false;
    result = distance + 1;
    return true;
  };

  View root{static_cast<const uint8_t *>(value),
            transportFourState ? static_cast<const uint8_t *>(unknown)
                               : nullptr,
            planeSize,
            transportWidth,
            0,
            transportFourState != 0};
  OBELISK_RT_TRY {
    struct Frame {
      uint32_t depth = 0;
      View view;
      bool initialized = false;
      int64_t kind = 0;
      int64_t sourceStart = 0;
      int64_t sourceStride = 0;
      uint64_t count = 0;
      uint64_t ordinal = 0;
      uint64_t childSpan = 0;
      bool childFourState = false;
      std::vector<uint8_t> childValue;
      std::vector<uint8_t> childUnknown;
    };
    std::vector<Frame> stack;
    stack.push_back({0, root});
    while (!stack.empty()) {
      Frame &frame = stack.back();
      if (frame.depth == dimensions) {
        const View &view = frame.view;
        if (view.span < elementWidth || view.base > view.planeSize * 8 ||
            elementWidth > view.planeSize * 8 - view.base ||
            elementCount == UINT64_MAX ||
            elementCount + 1 > UINT64_MAX / elementWidth)
          return OBELISK_RT_INVALID_ARGUMENT;
        uint64_t nextBits = (elementCount + 1) * elementWidth;
        uint64_t bytes = (nextBits + 7) / 8;
        if (bytes > SIZE_MAX)
          return OBELISK_RT_INVALID_ARGUMENT;
        flatValue.resize(static_cast<size_t>(bytes), 0);
        if (elementFourState)
          flatUnknown.resize(static_cast<size_t>(bytes), 0);
        copyBits(flatValue.data(), elementCount * elementWidth, view.value,
                 view.base, elementWidth);
        if (elementFourState && view.unknown)
          copyBits(flatUnknown.data(), elementCount * elementWidth,
                   view.unknown, view.base, elementWidth);
        ++elementCount;
        stack.pop_back();
        continue;
      }
      if (!frame.initialized) {
        const int64_t *record = shapePlan + uint64_t{frame.depth} * shapeWords;
        int64_t descriptorLeft = record[1];
        int64_t descriptorRight = record[2];
        int64_t childSpanWord = record[5];
        int64_t childFourStateWord = record[6];
        bool sizedFormal = record[7] != 0;
        frame.kind = record[0];
        frame.sourceStart = record[3];
        frame.sourceStride = record[4];
        if (frame.kind < 0 || frame.kind > 2 || descriptorLeft < INT32_MIN ||
            descriptorLeft > INT32_MAX || descriptorRight < INT32_MIN ||
            descriptorRight > INT32_MAX || frame.sourceStart < 0 ||
            childSpanWord <= 0 ||
            (childFourStateWord != 0 && childFourStateWord != 1) ||
            (record[7] != 0 && record[7] != 1))
          return OBELISK_RT_INVALID_ARGUMENT;
        frame.childSpan = static_cast<uint64_t>(childSpanWord);
        frame.childFourState = childFourStateWord != 0;
        auto &range = ranges[frame.depth];
        if (frame.kind == 0) {
          if (frame.sourceStride == 0 ||
              !checkedExtent(descriptorLeft, descriptorRight, frame.count))
            return OBELISK_RT_INVALID_ARGUMENT;
          range = {static_cast<int32_t>(descriptorLeft),
                   static_cast<int32_t>(descriptorRight), 0, 0, 0};
        } else {
          const View &view = frame.view;
          if (frame.sourceStart != 0 || frame.sourceStride != 0 ||
              view.span < 64 || view.base > view.planeSize * 8 ||
              64 > view.planeSize * 8 - view.base || (view.base & 7) != 0)
            return OBELISK_RT_INVALID_ARGUMENT;
          obelisk_rt_managed_word_v1 word = 0;
          std::memcpy(&word, view.value + view.base / 8, sizeof(word));
          obelisk_rt_object_v1 *container =
              obelisk_rt_object_from_managed_word(word);
          if (word != obelisk_rt_managed_word_from_object(container))
            return OBELISK_RT_INVALID_ARGUMENT;
          frame.count = obelisk_rt_v1_container_size(container);
          uint64_t expected = 0;
          if (sizedFormal &&
              (!checkedExtent(descriptorLeft, descriptorRight, expected) ||
               expected != frame.count))
            return OBELISK_RT_ARGUMENT_MISMATCH;
          if (!sizedFormal) {
            if (frame.count > static_cast<uint64_t>(INT32_MAX) + 1)
              return OBELISK_RT_INVALID_ARGUMENT;
            int32_t right =
                frame.count == 0 ? -1 : static_cast<int32_t>(frame.count - 1);
            if (runtimeKnown[frame.depth] && range.right != right)
              return OBELISK_RT_ARGUMENT_MISMATCH;
            range.left = 0;
            range.right = right;
            range.flags =
                OBELISK_RT_DPI_DIMENSION_RUNTIME |
                (frame.count == 0 ? OBELISK_RT_DPI_DIMENSION_EMPTY : 0);
            runtimeKnown[frame.depth] = 1;
          } else {
            range = {static_cast<int32_t>(descriptorLeft),
                     static_cast<int32_t>(descriptorRight), 0, 0, 0};
          }
          if (frame.count != 0) {
            if (frame.childSpan > UINT64_MAX / frame.count)
              return OBELISK_RT_INVALID_ARGUMENT;
            uint64_t bits = frame.childSpan * frame.count;
            uint64_t bytes = (bits + 7) / 8;
            if (bytes > SIZE_MAX)
              return OBELISK_RT_INVALID_ARGUMENT;
            frame.childValue.resize(static_cast<size_t>(bytes));
            if (frame.childFourState)
              frame.childUnknown.resize(static_cast<size_t>(bytes));
            status = obelisk_rt_v1_container_export_fixed(
                container, frame.childValue.data(),
                frame.childUnknown.empty() ? nullptr
                                           : frame.childUnknown.data(),
                bytes, bits, frame.childFourState ? 1 : 0, frame.childSpan,
                frame.count);
            if (status != OBELISK_RT_OK)
              return status;
          }
        }
        frame.initialized = true;
      }
      if (frame.ordinal == frame.count) {
        stack.pop_back();
        continue;
      }
      uint64_t ordinal = frame.ordinal++;
      View child;
      if (frame.kind == 0) {
        __int128 offset = static_cast<__int128>(frame.sourceStart) +
                          static_cast<__int128>(ordinal) * frame.sourceStride;
        if (offset < 0 || offset > UINT64_MAX ||
            static_cast<uint64_t>(offset) > frame.view.span ||
            frame.childSpan > frame.view.span - static_cast<uint64_t>(offset) ||
            static_cast<uint64_t>(offset) > UINT64_MAX - frame.view.base)
          return OBELISK_RT_INVALID_ARGUMENT;
        child = {frame.view.value,
                 frame.view.unknown,
                 frame.view.planeSize,
                 frame.childSpan,
                 frame.view.base + static_cast<uint64_t>(offset),
                 frame.childFourState};
      } else {
        child = {frame.childValue.data(),
                 frame.childUnknown.empty() ? nullptr
                                            : frame.childUnknown.data(),
                 frame.childValue.size(),
                 frame.childSpan,
                 ordinal * frame.childSpan,
                 frame.childFourState};
      }
      stack.push_back({frame.depth + 1, child});
    }
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  for (uint32_t depth = 0; depth != dimensions; ++depth) {
    const int64_t *record = shapePlan + uint64_t{depth} * shapeWords;
    if (record[0] != 0 && record[7] == 0 && !runtimeKnown[depth])
      ranges[depth] = {
          0, -1, 0,
          OBELISK_RT_DPI_DIMENSION_RUNTIME | OBELISK_RT_DPI_DIMENSION_EMPTY, 0};
  }

  uint64_t elementSize = canonicalElementSize(elementCategory, elementWidth);
  if (elementCategory == 8 || elementCategory == 9 || elementCategory == 13)
    elementSize = elementCSize;
  if (elementSize == 0 || elementCount > UINT64_MAX / elementSize ||
      elementCount > UINT64_MAX / elementWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t dataSize = elementCount * elementSize;
  if (elementCount != 0 &&
      elementStringCount > (UINT64_MAX - dataSize) / 8 / elementCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t capacity = dataSize + elementCount * elementStringCount * 8;
  uint64_t stride = elementSize;
  for (uint32_t depth = dimensions; depth-- != 0;) {
    ranges[depth].byte_stride = stride;
    uint64_t count = 0;
    if ((ranges[depth].flags & OBELISK_RT_DPI_DIMENSION_EMPTY) != 0)
      count = 0;
    else if (!checkedExtent(ranges[depth].left, ranges[depth].right, count))
      return OBELISK_RT_INVALID_ARGUMENT;
    if (count != 0 && stride > UINT64_MAX / count)
      return OBELISK_RT_INVALID_ARGUMENT;
    stride *= count;
  }
  uint64_t rangeBytes = uint64_t{dimensions} * sizeof(ranges[0]);
  if (rangeBytes > SIZE_MAX ||
      elementCAlignment - 1 > UINT64_MAX - rangeBytes ||
      capacity > UINT64_MAX - rangeBytes - (elementCAlignment - 1))
    return OBELISK_RT_OUT_OF_RESOURCES;
  uint64_t allocationSize = rangeBytes + (elementCAlignment - 1) + capacity;
  void *allocation = std::malloc(static_cast<size_t>(allocationSize));
  if (!allocation)
    return OBELISK_RT_OUT_OF_MEMORY;
  auto *storedRanges = static_cast<obelisk_rt_dpi_dimension_v1 *>(allocation);
  std::memcpy(storedRanges, ranges.data(), static_cast<size_t>(rangeBytes));
  uintptr_t dataAddress = reinterpret_cast<uintptr_t>(allocation) + rangeBytes;
  dataAddress = (dataAddress + elementCAlignment - 1) &
                ~(uintptr_t{elementCAlignment} - 1);
  void *data = capacity == 0 ? nullptr : reinterpret_cast<void *>(dataAddress);
  if (elementCount != 0) {
    status = obelisk_rt_v1_dpi_open_array_aggregate_pack(
        flatValue.data(), flatUnknown.empty() ? nullptr : flatUnknown.data(),
        (elementCount * elementWidth + 7) / 8, elementCount * elementWidth,
        elementFourState, elementWidth, elementCount, 0, nullptr, 0, data,
        dataSize, capacity, elementCSize, elementStringCount, elementPlan,
        elementPlanWords);
    if (status != OBELISK_RT_OK) {
      std::free(allocation);
      return status;
    }
  }
  uint32_t flags = OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT;
  if (writable)
    flags |= OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE;
  if (elementCategory <= 7)
    flags |= OBELISK_RT_DPI_OPEN_ARRAY_PACKED;
  if (elementFourState)
    flags |= OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE;
  if (elementCount == 0)
    flags |= OBELISK_RT_DPI_OPEN_ARRAY_EMPTY;
  outStorage->descriptor = {OBELISK_RT_DPI_OPEN_ARRAY_MAGIC,
                            flags,
                            dimensions,
                            elementWidth,
                            packedLeft,
                            packedRight,
                            elementSize,
                            data,
                            dataSize,
                            storedRanges,
                            0};
  outStorage->allocation = allocation;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_dpi_open_array_finish_recursive(
    obelisk_rt_status callStatus, obelisk_rt_context *context,
    const obelisk_rt_dpi_open_array_storage_v1 *storage, void *value,
    void *unknown, uint64_t planeSize, uint64_t transportWidth,
    uint32_t transportFourState, uint32_t elementWidth,
    uint32_t elementFourState, uint64_t elementCSize,
    const int64_t *elementPlan, uint64_t elementPlanWords,
    const int64_t *shapePlan, uint32_t dimensions) {
  constexpr uint64_t shapeWords = 8;
  if (callStatus != OBELISK_RT_OK)
    return callStatus;
  if (!context || !storage || !value || !shapePlan || !elementPlan ||
      dimensions == 0 || storage->descriptor.dimensions != dimensions ||
      transportWidth == 0 || elementWidth == 0 || transportFourState > 1 ||
      elementFourState > 1 || (transportFourState && !unknown) ||
      planeSize < (transportWidth + 7) / 8 || elementCSize == 0 ||
      elementPlanWords == 0 || elementPlanWords % 8 != 0)
    return OBELISK_RT_INVALID_ARGUMENT;

  uint64_t elementCount = 1;
  for (uint32_t depth = 0; depth != dimensions; ++depth) {
    const auto &range = storage->descriptor.ranges[depth];
    if ((range.flags & OBELISK_RT_DPI_DIMENSION_EMPTY) != 0) {
      elementCount = 0;
      break;
    }
    uint64_t count = 0;
    if (!extent(range.left, range.right, count) ||
        (count != 0 && elementCount > UINT64_MAX / count))
      return OBELISK_RT_INVALID_ARGUMENT;
    elementCount *= count;
  }
  if (elementCount > UINT64_MAX / elementWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t flatWidth = elementCount * elementWidth;
  uint64_t flatBytes = (flatWidth + 7) / 8;
  if (flatBytes > SIZE_MAX)
    return OBELISK_RT_OUT_OF_RESOURCES;
  std::vector<uint8_t> flatValue(static_cast<size_t>(flatBytes));
  std::vector<uint8_t> flatUnknown;
  if (elementFourState)
    flatUnknown.resize(static_cast<size_t>(flatBytes));
  if (elementCount != 0) {
    obelisk_rt_status status = obelisk_rt_v1_dpi_open_array_aggregate_unpack(
        context, storage->descriptor.data, storage->descriptor.data_size,
        elementCSize, elementPlan, elementPlanWords, elementCount, 0, nullptr,
        0, flatValue.data(), flatUnknown.empty() ? nullptr : flatUnknown.data(),
        flatBytes, flatWidth, elementFourState, elementWidth);
    if (status != OBELISK_RT_OK)
      return status;
  }
  void *aggregateRoots = nullptr;
  obelisk_rt_status rootStatus =
      obelisk_rt_v1_dpi_open_array_aggregate_roots_push(
          context, flatValue.data(), flatBytes, flatWidth, elementWidth,
          elementCount, 0, nullptr, 0, elementCSize, elementPlan,
          elementPlanWords, &aggregateRoots);
  if (rootStatus != OBELISK_RT_OK)
    return rootStatus;
  struct AggregateRootGuard {
    obelisk_rt_context *context;
    void *&handle;
    ~AggregateRootGuard() {
      if (handle)
        (void)obelisk_rt_v1_dpi_aggregate_roots_pop(context, handle);
    }
    obelisk_rt_status pop() {
      obelisk_rt_status result =
          obelisk_rt_v1_dpi_aggregate_roots_pop(context, handle);
      if (result == OBELISK_RT_OK)
        handle = nullptr;
      return result;
    }
  } rootGuard{context, aggregateRoots};
  obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
  if (!lane)
    return OBELISK_RT_INVALID_LIFECYCLE;

  struct MutableView {
    uint8_t *value;
    uint8_t *unknown;
    uint64_t planeSize;
    uint64_t span;
    uint64_t base;
    bool fourState;
  };
  uint64_t leaf = 0;
  obelisk_rt_status status = OBELISK_RT_OK;
  auto copyBits = [](uint8_t *destination, uint64_t destinationBit,
                     const uint8_t *source, uint64_t sourceBit,
                     uint64_t width) {
    if (((destinationBit | sourceBit | width) & 7) == 0) {
      std::memcpy(destination + destinationBit / 8, source + sourceBit / 8,
                  static_cast<size_t>(width / 8));
      return;
    }
    for (uint64_t bit = 0; bit != width; ++bit)
      setBit(destination, destinationBit + bit, bitAt(source, sourceBit + bit));
  };
  OBELISK_RT_TRY {
    MutableView root{static_cast<uint8_t *>(value),
                     transportFourState ? static_cast<uint8_t *>(unknown)
                                        : nullptr,
                     planeSize,
                     transportWidth,
                     0,
                     transportFourState != 0};
    struct Frame {
      uint32_t depth = 0;
      MutableView view;
      bool initialized = false;
      int64_t kind = 0;
      int64_t sourceStart = 0;
      int64_t sourceStride = 0;
      uint64_t count = 0;
      uint64_t ordinal = 0;
      uint64_t childSpan = 0;
      bool childFourState = false;
      obelisk_rt_object_v1 *container = nullptr;
      std::vector<uint8_t> childValue;
      std::vector<uint8_t> childUnknown;
    };
    std::vector<Frame> stack;
    stack.push_back({0, root});
    while (!stack.empty()) {
      Frame &frame = stack.back();
      if (frame.depth == dimensions) {
        MutableView view = frame.view;
        if (leaf >= elementCount || view.span < elementWidth ||
            view.base > view.planeSize * 8 ||
            elementWidth > view.planeSize * 8 - view.base ||
            (elementFourState && !view.unknown))
          return OBELISK_RT_INVALID_ARGUMENT;
        copyBits(view.value, view.base, flatValue.data(), leaf * elementWidth,
                 elementWidth);
        if (elementFourState)
          copyBits(view.unknown, view.base, flatUnknown.data(),
                   leaf * elementWidth, elementWidth);
        ++leaf;
        stack.pop_back();
        continue;
      }
      if (!frame.initialized) {
        const int64_t *record = shapePlan + uint64_t{frame.depth} * shapeWords;
        frame.kind = record[0];
        frame.sourceStart = record[3];
        frame.sourceStride = record[4];
        int64_t childSpanWord = record[5];
        int64_t childFourStateWord = record[6];
        if (frame.kind < 0 || frame.kind > 2 || frame.sourceStart < 0 ||
            childSpanWord <= 0 ||
            (childFourStateWord != 0 && childFourStateWord != 1))
          return OBELISK_RT_INVALID_ARGUMENT;
        frame.childSpan = static_cast<uint64_t>(childSpanWord);
        frame.childFourState = childFourStateWord != 0;
        const auto &range = storage->descriptor.ranges[frame.depth];
        if ((range.flags & OBELISK_RT_DPI_DIMENSION_EMPTY) == 0 &&
            !extent(range.left, range.right, frame.count))
          return OBELISK_RT_INVALID_ARGUMENT;
        if ((range.flags & OBELISK_RT_DPI_DIMENSION_EMPTY) != 0)
          frame.count = 0;
        if (frame.kind == 0) {
          if (frame.sourceStride == 0)
            return OBELISK_RT_INVALID_ARGUMENT;
        } else {
          MutableView view = frame.view;
          if (frame.sourceStart != 0 || frame.sourceStride != 0 ||
              view.span < 64 || (view.base & 7) != 0 ||
              view.base > view.planeSize * 8 ||
              64 > view.planeSize * 8 - view.base)
            return OBELISK_RT_INVALID_ARGUMENT;
          obelisk_rt_managed_word_v1 word = 0;
          std::memcpy(&word, view.value + view.base / 8, sizeof(word));
          frame.container = obelisk_rt_object_from_managed_word(word);
          if (word != obelisk_rt_managed_word_from_object(frame.container) ||
              obelisk_rt_v1_container_size(frame.container) != frame.count ||
              (frame.count != 0 && frame.childSpan > UINT64_MAX / frame.count))
            return OBELISK_RT_INVALID_ARGUMENT;
          if (frame.count != 0) {
            uint64_t bits = frame.childSpan * frame.count;
            uint64_t bytes = (bits + 7) / 8;
            if (bytes > SIZE_MAX)
              return OBELISK_RT_INVALID_ARGUMENT;
            frame.childValue.resize(static_cast<size_t>(bytes));
            if (frame.childFourState)
              frame.childUnknown.resize(static_cast<size_t>(bytes));
            status = obelisk_rt_v1_container_export_fixed(
                frame.container, frame.childValue.data(),
                frame.childUnknown.empty() ? nullptr
                                           : frame.childUnknown.data(),
                bytes, bits, frame.childFourState ? 1 : 0, frame.childSpan,
                frame.count);
            if (status != OBELISK_RT_OK)
              return status;
          }
        }
        frame.initialized = true;
      }
      if (frame.ordinal == frame.count) {
        if (frame.kind != 0 && frame.count != 0) {
          uint64_t bits = frame.childSpan * frame.count;
          status = obelisk_rt_v1_container_import_fixed(
              lane, frame.container, frame.childValue.data(),
              frame.childUnknown.empty() ? nullptr : frame.childUnknown.data(),
              frame.childValue.size(), bits, frame.childFourState ? 1 : 0,
              frame.childSpan, frame.count);
          if (status != OBELISK_RT_OK)
            return status;
        }
        stack.pop_back();
        continue;
      }
      uint64_t ordinal = frame.ordinal++;
      MutableView child;
      if (frame.kind == 0) {
        __int128 offset = static_cast<__int128>(frame.sourceStart) +
                          static_cast<__int128>(ordinal) * frame.sourceStride;
        if (offset < 0 || offset > UINT64_MAX ||
            static_cast<uint64_t>(offset) > frame.view.span ||
            frame.childSpan > frame.view.span - static_cast<uint64_t>(offset) ||
            static_cast<uint64_t>(offset) > UINT64_MAX - frame.view.base)
          return OBELISK_RT_INVALID_ARGUMENT;
        child = {frame.view.value,
                 frame.view.unknown,
                 frame.view.planeSize,
                 frame.childSpan,
                 frame.view.base + static_cast<uint64_t>(offset),
                 frame.childFourState};
      } else {
        child = {frame.childValue.data(),
                 frame.childUnknown.empty() ? nullptr
                                            : frame.childUnknown.data(),
                 frame.childValue.size(),
                 frame.childSpan,
                 ordinal * frame.childSpan,
                 frame.childFourState};
      }
      stack.push_back({frame.depth + 1, child});
    }
    if (leaf != elementCount)
      return OBELISK_RT_INVALID_ARGUMENT;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  return rootGuard.pop();
}

extern "C" int svLeft(const svOpenArrayHandle handle, int dimension) {
  const auto *array = descriptor(handle);
  int32_t left = 0, right = 0;
  return array && range(*array, dimension, left, right) ? left : 0;
}

extern "C" int svRight(const svOpenArrayHandle handle, int dimension) {
  const auto *array = descriptor(handle);
  int32_t left = 0, right = 0;
  return array && range(*array, dimension, left, right) ? right : 0;
}

extern "C" int svLow(const svOpenArrayHandle handle, int dimension) {
  const auto *array = descriptor(handle);
  int32_t left = 0, right = 0;
  if (array)
    if (const auto *record = dimensionRecord(*array, dimension);
        record && (record->flags & OBELISK_RT_DPI_DIMENSION_RUNTIME) != 0)
      return record->left;
  return array && range(*array, dimension, left, right) ? std::min(left, right)
                                                        : 0;
}

extern "C" int svHigh(const svOpenArrayHandle handle, int dimension) {
  const auto *array = descriptor(handle);
  int32_t left = 0, right = 0;
  if (array)
    if (const auto *record = dimensionRecord(*array, dimension);
        record && (record->flags & OBELISK_RT_DPI_DIMENSION_RUNTIME) != 0)
      return record->right;
  return array && range(*array, dimension, left, right) ? std::max(left, right)
                                                        : 0;
}

extern "C" int svIncrement(const svOpenArrayHandle handle, int dimension) {
  const auto *array = descriptor(handle);
  int32_t left = 0, right = 0;
  if (array)
    if (const auto *record = dimensionRecord(*array, dimension);
        record && (record->flags & OBELISK_RT_DPI_DIMENSION_RUNTIME) != 0)
      return -1;
  return array && range(*array, dimension, left, right)
             ? (left >= right ? 1 : -1)
             : 0;
}

extern "C" int svSize(const svOpenArrayHandle handle, int dimension) {
  const auto *array = descriptor(handle);
  int32_t left = 0, right = 0;
  uint64_t size = 0;
  if (!array || !range(*array, dimension, left, right) ||
      !extent(left, right, size) || size > INT32_MAX)
    return 0;
  if (const auto *record = dimensionRecord(*array, dimension);
      record && (record->flags & OBELISK_RT_DPI_DIMENSION_EMPTY) != 0)
    return 0;
  return static_cast<int>(size);
}

extern "C" int svDimensions(const svOpenArrayHandle handle) {
  const auto *array = descriptor(handle);
  return array && array->dimensions <= INT32_MAX
             ? static_cast<int>(array->dimensions)
             : 0;
}

extern "C" void *svGetArrayPtr(const svOpenArrayHandle handle) {
  const auto *array = descriptor(handle);
  return array && (array->flags & OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT) != 0
             ? array->data
             : nullptr;
}

extern "C" int svSizeOfArray(const svOpenArrayHandle handle) {
  const auto *array = descriptor(handle);
  return array && (array->flags & OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT) != 0 &&
                 array->data_size <= INT32_MAX
             ? static_cast<int>(array->data_size)
             : 0;
}

extern "C" void *svGetArrElemPtr(const svOpenArrayHandle handle, int index,
                                 ...) {
  const auto *array = descriptor(handle);
  if (!array || (array->flags & OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT) == 0)
    return nullptr;
  va_list arguments;
  va_start(arguments, index);
  void *result = elementPointer(*array, array->dimensions, index, arguments);
  va_end(arguments);
  return result;
}

extern "C" void *svGetArrElemPtr1(const svOpenArrayHandle handle, int index) {
  int indices[] = {index};
  const auto *array = descriptor(handle);
  return array && (array->flags & OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT) != 0
             ? elementPointer(*array, 1, indices)
             : nullptr;
}

extern "C" void *svGetArrElemPtr2(const svOpenArrayHandle handle, int first,
                                  int second) {
  int indices[] = {first, second};
  const auto *array = descriptor(handle);
  return array && (array->flags & OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT) != 0
             ? elementPointer(*array, 2, indices)
             : nullptr;
}

extern "C" void *svGetArrElemPtr3(const svOpenArrayHandle handle, int first,
                                  int second, int third) {
  int indices[] = {first, second, third};
  const auto *array = descriptor(handle);
  return array && (array->flags & OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT) != 0
             ? elementPointer(*array, 3, indices)
             : nullptr;
}

#define OBELISK_DEFINE_VECTOR_ACCESS(Name, Logic, Type)                        \
  extern "C" void svPut##Name##ArrElemVecVal(                                  \
      const svOpenArrayHandle handle, const Type *source, int index, ...) {    \
    va_list arguments;                                                         \
    va_start(arguments, index);                                                \
    withVariadicElement(                                                       \
        handle, index, arguments, [&](const auto &array, void *element) {      \
          if ((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE) != 0)         \
            copyVector(element, source, array, Logic);                         \
        });                                                                    \
    va_end(arguments);                                                         \
  }                                                                            \
  extern "C" void svPut##Name##ArrElem1VecVal(const svOpenArrayHandle handle,  \
                                              const Type *source, int first) { \
    int indices[] = {first};                                                   \
    withFixedElement(handle, 1, indices, [&](const auto &array, void *elem) {  \
      if ((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE) != 0)             \
        copyVector(elem, source, array, Logic);                                \
    });                                                                        \
  }                                                                            \
  extern "C" void svPut##Name##ArrElem2VecVal(const svOpenArrayHandle handle,  \
                                              const Type *source, int first,   \
                                              int second) {                    \
    int indices[] = {first, second};                                           \
    withFixedElement(handle, 2, indices, [&](const auto &array, void *elem) {  \
      if ((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE) != 0)             \
        copyVector(elem, source, array, Logic);                                \
    });                                                                        \
  }                                                                            \
  extern "C" void svPut##Name##ArrElem3VecVal(const svOpenArrayHandle handle,  \
                                              const Type *source, int first,   \
                                              int second, int third) {         \
    int indices[] = {first, second, third};                                    \
    withFixedElement(handle, 3, indices, [&](const auto &array, void *elem) {  \
      if ((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE) != 0)             \
        copyVector(elem, source, array, Logic);                                \
    });                                                                        \
  }                                                                            \
  extern "C" void svGet##Name##ArrElemVecVal(                                  \
      Type *destination, const svOpenArrayHandle handle, int index, ...) {     \
    va_list arguments;                                                         \
    va_start(arguments, index);                                                \
    withVariadicElement(handle, index, arguments,                              \
                        [&](const auto &array, void *element) {                \
                          copyVector(destination, element, array, Logic);      \
                        });                                                    \
    va_end(arguments);                                                         \
  }                                                                            \
  extern "C" void svGet##Name##ArrElem1VecVal(                                 \
      Type *destination, const svOpenArrayHandle handle, int first) {          \
    int indices[] = {first};                                                   \
    withFixedElement(handle, 1, indices, [&](const auto &array, void *elem) {  \
      copyVector(destination, elem, array, Logic);                             \
    });                                                                        \
  }                                                                            \
  extern "C" void svGet##Name##ArrElem2VecVal(Type *destination,               \
                                              const svOpenArrayHandle handle,  \
                                              int first, int second) {         \
    int indices[] = {first, second};                                           \
    withFixedElement(handle, 2, indices, [&](const auto &array, void *elem) {  \
      copyVector(destination, elem, array, Logic);                             \
    });                                                                        \
  }                                                                            \
  extern "C" void svGet##Name##ArrElem3VecVal(                                 \
      Type *destination, const svOpenArrayHandle handle, int first,            \
      int second, int third) {                                                 \
    int indices[] = {first, second, third};                                    \
    withFixedElement(handle, 3, indices, [&](const auto &array, void *elem) {  \
      copyVector(destination, elem, array, Logic);                             \
    });                                                                        \
  }

OBELISK_DEFINE_VECTOR_ACCESS(Bit, false, svBitVecVal)
OBELISK_DEFINE_VECTOR_ACCESS(Logic, true, svLogicVecVal)

#undef OBELISK_DEFINE_VECTOR_ACCESS

namespace {
template <typename Scalar>
Scalar getScalar(svOpenArrayHandle handle, uint32_t count, const int *indices,
                 bool logic) {
  Scalar result = 0;
  withFixedElement(handle, count, indices, [&](const auto &array, void *elem) {
    if (array.element_bit_width == 1 &&
        (((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE) != 0) == logic))
      std::memcpy(&result, elem, sizeof(result));
  });
  return result;
}

template <typename Scalar>
void putScalar(svOpenArrayHandle handle, Scalar value, uint32_t count,
               const int *indices, bool logic) {
  withFixedElement(handle, count, indices, [&](const auto &array, void *elem) {
    if ((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE) != 0 &&
        array.element_bit_width == 1 &&
        (((array.flags & OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE) != 0) == logic))
      std::memcpy(elem, &value, sizeof(value));
  });
}
} // namespace

#define OBELISK_DEFINE_SCALAR_ACCESS(Name, Type, Logic)                        \
  extern "C" Type svGet##Name##ArrElem(const svOpenArrayHandle handle,         \
                                       int index, ...) {                       \
    const auto *array = descriptor(handle);                                    \
    Type result = 0;                                                           \
    if (!array)                                                                \
      return result;                                                           \
    va_list arguments;                                                         \
    va_start(arguments, index);                                                \
    void *elem = elementPointer(*array, array->dimensions, index, arguments);  \
    va_end(arguments);                                                         \
    if (elem && array->element_bit_width == 1 &&                               \
        (((array->flags & OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE) != 0) ==       \
         Logic))                                                               \
      std::memcpy(&result, elem, sizeof(result));                              \
    return result;                                                             \
  }                                                                            \
  extern "C" Type svGet##Name##ArrElem1(const svOpenArrayHandle handle,        \
                                        int first) {                           \
    int indices[] = {first};                                                   \
    return getScalar<Type>(handle, 1, indices, Logic);                         \
  }                                                                            \
  extern "C" Type svGet##Name##ArrElem2(const svOpenArrayHandle handle,        \
                                        int first, int second) {               \
    int indices[] = {first, second};                                           \
    return getScalar<Type>(handle, 2, indices, Logic);                         \
  }                                                                            \
  extern "C" Type svGet##Name##ArrElem3(const svOpenArrayHandle handle,        \
                                        int first, int second, int third) {    \
    int indices[] = {first, second, third};                                    \
    return getScalar<Type>(handle, 3, indices, Logic);                         \
  }                                                                            \
  extern "C" void svPut##Name##ArrElem(const svOpenArrayHandle handle,         \
                                       Type value, int index, ...) {           \
    const auto *array = descriptor(handle);                                    \
    if (!array)                                                                \
      return;                                                                  \
    va_list arguments;                                                         \
    va_start(arguments, index);                                                \
    void *elem = elementPointer(*array, array->dimensions, index, arguments);  \
    va_end(arguments);                                                         \
    if (elem && (array->flags & OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE) != 0 &&    \
        array->element_bit_width == 1 &&                                       \
        (((array->flags & OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE) != 0) ==       \
         Logic))                                                               \
      std::memcpy(elem, &value, sizeof(value));                                \
  }                                                                            \
  extern "C" void svPut##Name##ArrElem1(const svOpenArrayHandle handle,        \
                                        Type value, int first) {               \
    int indices[] = {first};                                                   \
    putScalar(handle, value, 1, indices, Logic);                               \
  }                                                                            \
  extern "C" void svPut##Name##ArrElem2(const svOpenArrayHandle handle,        \
                                        Type value, int first, int second) {   \
    int indices[] = {first, second};                                           \
    putScalar(handle, value, 2, indices, Logic);                               \
  }                                                                            \
  extern "C" void svPut##Name##ArrElem3(const svOpenArrayHandle handle,        \
                                        Type value, int first, int second,     \
                                        int third) {                           \
    int indices[] = {first, second, third};                                    \
    putScalar(handle, value, 3, indices, Logic);                               \
  }

OBELISK_DEFINE_SCALAR_ACCESS(Bit, svBit, false)
OBELISK_DEFINE_SCALAR_ACCESS(Logic, svLogic, true)

#undef OBELISK_DEFINE_SCALAR_ACCESS
