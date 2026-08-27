//===- ContainerBitstream.cpp - Feature-local bit-stream export -----------===//

#include "ContainerStorageInternal.h"
#include "RuntimeInternal.h"

#include <array>
#include <cstring>
#include <memory>

namespace {

using obelisk::runtime_detail::BufferHeader;
using obelisk::runtime_detail::ContainerHeader;

OBELISK_RT_FEATURE_HELPER void
copyBits(uint8_t *destination, uint64_t destinationOffset,
         const uint8_t *source, const uint8_t *sourceUnknown,
         uint64_t sourceOffset, uint64_t width, bool outputFourState,
         uint8_t *outputUnknown) {
  if (((destinationOffset | sourceOffset | width) & 7) == 0) {
    uint8_t *output = destination + destinationOffset / 8;
    source += sourceOffset / 8;
    if (sourceUnknown)
      sourceUnknown += sourceOffset / 8;
    size_t bytes = static_cast<size_t>(width / 8);
    if (!sourceUnknown || outputFourState)
      std::memcpy(output, source, bytes);
    else
      for (size_t byte = 0; byte != bytes; ++byte)
        output[byte] =
            static_cast<uint8_t>(source[byte] & ~sourceUnknown[byte]);
    if (outputFourState && sourceUnknown)
      std::memcpy(outputUnknown + destinationOffset / 8, sourceUnknown, bytes);
    return;
  }
  for (uint64_t bit = 0; bit != width; ++bit) {
    uint64_t sourceBit = sourceOffset + bit;
    uint8_t sourceMask = static_cast<uint8_t>(1u << (sourceBit & 7));
    uint64_t destinationBit = destinationOffset + bit;
    uint8_t destinationMask = static_cast<uint8_t>(1u << (destinationBit & 7));
    uint8_t &output = destination[destinationBit / 8];
    bool unknown = sourceUnknown && (sourceUnknown[sourceBit / 8] & sourceMask);
    if ((source[sourceBit / 8] & sourceMask) && (!unknown || outputFourState))
      output |= destinationMask;
    else
      output &= static_cast<uint8_t>(~destinationMask);
    if (outputFourState && unknown)
      outputUnknown[destinationBit / 8] |= destinationMask;
  }
}

struct ExportRequest {
  void *value;
  void *unknown;
  uint64_t planeSize;
  uint64_t bitWidth;
  uint32_t fourState;
  uint64_t elementWidth;
  uint64_t count;
  uint64_t elementPlaneSize;
  uint32_t elementFourState;
};

struct BufferRequest {
  ContainerHeader *header;
  ExportRequest *request;
};

OBELISK_RT_FEATURE_HELPER obelisk_rt_status packBuffer(void *opaque,
                                                       uint8_t *buffer,
                                                       uint64_t extent) {
  auto *environment = static_cast<BufferRequest *>(opaque);
  ContainerHeader &header = *environment->header;
  ExportRequest &request = *environment->request;
  if (extent < sizeof(BufferHeader) ||
      reinterpret_cast<BufferHeader *>(buffer)->reserved != 0)
    return OBELISK_RT_INVALID_HANDLE;
  if (request.elementFourState && request.elementPlaneSize > UINT64_MAX / 2)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t stride =
      request.elementPlaneSize * (request.elementFourState ? 2 : 1);
  if (stride == 0 || header.capacity > (extent - sizeof(BufferHeader)) / stride)
    return OBELISK_RT_INVALID_HANDLE;
  uint8_t *data = buffer + sizeof(BufferHeader);
  for (uint64_t ordinal = 0; ordinal != request.count; ++ordinal) {
    uint64_t physical = header.kind == OBELISK_RT_CONTAINER_QUEUE
                            ? (header.head + ordinal) & (header.capacity - 1)
                            : ordinal;
    const uint8_t *source = data + physical * stride;
    const uint8_t *sourceUnknown =
        request.elementFourState ? source + request.elementPlaneSize : nullptr;
    uint64_t destination = (request.count - ordinal - 1) * request.elementWidth;
    copyBits(static_cast<uint8_t *>(request.value), destination, source,
             sourceUnknown, 0, request.elementWidth, request.fourState != 0,
             static_cast<uint8_t *>(request.unknown));
  }
  return OBELISK_RT_OK;
}

OBELISK_RT_FEATURE_HELPER obelisk_rt_status packContainer(void *opaque,
                                                          uint8_t *object,
                                                          uint64_t extent) {
  if (extent != sizeof(ContainerHeader))
    return OBELISK_RT_INVALID_HANDLE;
  auto *header = reinterpret_cast<ContainerHeader *>(object);
  auto *request = static_cast<ExportRequest *>(opaque);
  bool sequential = header->kind == OBELISK_RT_CONTAINER_DYNAMIC_ARRAY ||
                    header->kind == OBELISK_RT_CONTAINER_QUEUE;
  bool fourState = header->element && (header->element->flags &
                                       OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
  if (!sequential || !header->element ||
      (header->element->kind != OBELISK_RT_ELEMENT_BITS &&
       header->element->kind != OBELISK_RT_ELEMENT_LOGIC) ||
      (header->element->kind == OBELISK_RT_ELEMENT_LOGIC) != fourState ||
      (header->element->flags & ~(OBELISK_RT_ELEMENT_FOUR_STATE |
                                  OBELISK_RT_ELEMENT_SIGNED)) != 0 ||
      header->element->bit_width != request->elementWidth ||
      header->element->value_size != request->elementPlaneSize ||
      fourState != (request->elementFourState != 0) ||
      header->size != request->count || header->size > header->capacity ||
      !header->buffer ||
      (header->kind == OBELISK_RT_CONTAINER_QUEUE &&
       (header->capacity == 0 ||
        (header->capacity & (header->capacity - 1)) != 0 ||
        header->head >= header->capacity)))
    return OBELISK_RT_ARGUMENT_MISMATCH;
  BufferRequest bufferRequest{header, request};
  return obelisk_rt_managed_object_access(
      header->buffer, OBELISK_RT_MANAGED_BUFFER, packBuffer, &bufferRequest);
}

} // namespace

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_v1_container_export_bitstream(obelisk_rt_object_v1 *container,
                                         void *outValue, void *outUnknown,
                                         uint64_t planeSize, uint64_t bitWidth,
                                         uint32_t fourState,
                                         uint64_t elementWidth, uint64_t count,
                                         uint64_t elementPlaneSize,
                                         uint32_t elementFourState) {
  if (!container || !outValue || planeSize == 0 || bitWidth == 0 ||
      planeSize < bitWidth / 8 + ((bitWidth & 7) != 0) || fourState > 1 ||
      (fourState && !outUnknown) || elementWidth == 0 ||
      bitWidth % elementWidth != 0 || count != bitWidth / elementWidth ||
      elementPlaneSize == 0 || elementFourState > 1 || planeSize > SIZE_MAX ||
      elementPlaneSize > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outValue, 0, static_cast<size_t>(planeSize));
  if (fourState)
    std::memset(outUnknown, 0, static_cast<size_t>(planeSize));
  ExportRequest request{outValue, outUnknown,       planeSize,
                        bitWidth, fourState,        elementWidth,
                        count,    elementPlaneSize, elementFourState};
  return obelisk_rt_managed_object_access(
      container, OBELISK_RT_MANAGED_CONTAINER, packContainer, &request);
}

namespace {

OBELISK_RT_FEATURE_HELPER uint64_t readPlan64(const uint8_t *bytes) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
  return value;
}

struct PlanRecord {
  uint32_t opcode;
  uint32_t bodyRecords;
  uint64_t sourceOffset;
  uint64_t extent;
  uint64_t stride;
  uint64_t sourceSpan;
  uint64_t outputWidth;
};

OBELISK_RT_FEATURE_HELPER PlanRecord readRecord(const uint8_t *records,
                                                uint64_t index) {
  const uint8_t *record = records + index * 48;
  uint64_t operation = readPlan64(record);
  return {
      static_cast<uint32_t>(operation), static_cast<uint32_t>(operation >> 32),
      readPlan64(record + 8),           readPlan64(record + 16),
      readPlan64(record + 24),          readPlan64(record + 32),
      readPlan64(record + 40)};
}

struct PlanFrame {
  uint64_t index;
  uint64_t end;
  union {
    struct {
      uint64_t sourceSpan;
      uint64_t output;
      uint64_t expectedOutput;
      uint64_t sourceCursor;
    } validation;
    struct {
      uint64_t sourceBase;
      uint64_t repeat;
      uint64_t repeatCount;
      uint64_t repeatBase;
      uint64_t repeatStride;
    } execution;
  } state;
};

enum class PlanValidation { Invalid, Valid, NeedsDepth };

OBELISK_RT_FEATURE_HELPER PlanValidation
validatePlan(const uint8_t *records, uint64_t recordCount, uint64_t sourceSpan,
             uint64_t outputWidth, PlanFrame *stack, uint64_t capacity) {
  uint64_t depth = 0;
  stack[0].index = 0;
  stack[0].end = recordCount;
  stack[0].state.validation = {sourceSpan, 0, outputWidth, 0};
  while (true) {
    PlanFrame &frame = stack[depth];
    auto &validation = frame.state.validation;
    if (frame.index == frame.end) {
      if (validation.output != validation.expectedOutput ||
          validation.sourceCursor != validation.sourceSpan)
        return PlanValidation::Invalid;
      if (depth == 0)
        return PlanValidation::Valid;
      --depth;
      continue;
    }
    if (frame.index > frame.end)
      return PlanValidation::Invalid;
    uint64_t index = frame.index++;
    PlanRecord record = readRecord(records, index);
    if (record.opcode == OBELISK_RT_AGGREGATE_BITSTREAM_COPY) {
      if (record.bodyRecords != 0 || record.extent == 0 || record.stride != 0 ||
          record.sourceSpan != 0 || record.outputWidth != record.extent ||
          record.sourceOffset != validation.sourceCursor ||
          record.sourceOffset > validation.sourceSpan ||
          record.extent > validation.sourceSpan - record.sourceOffset ||
          record.extent > UINT64_MAX - validation.output ||
          record.extent > UINT64_MAX - validation.sourceCursor)
        return PlanValidation::Invalid;
      validation.output += record.extent;
      validation.sourceCursor += record.extent;
      continue;
    }
    if (record.opcode != OBELISK_RT_AGGREGATE_BITSTREAM_REPEAT ||
        record.bodyRecords == 0 || record.extent == 0 ||
        record.sourceSpan == 0 || record.outputWidth == 0 ||
        record.bodyRecords > frame.end - frame.index ||
        record.sourceOffset != validation.sourceCursor ||
        record.stride != record.sourceSpan ||
        record.sourceSpan != record.outputWidth)
      return PlanValidation::Invalid;
    uint64_t bodyEnd = frame.index + record.bodyRecords;
    uint64_t last = record.extent - 1;
    if ((record.stride && last > UINT64_MAX / record.stride) ||
        record.sourceOffset > validation.sourceSpan)
      return PlanValidation::Invalid;
    uint64_t lastOffset = last * record.stride;
    if (lastOffset > validation.sourceSpan - record.sourceOffset ||
        record.sourceSpan >
            validation.sourceSpan - record.sourceOffset - lastOffset ||
        record.outputWidth > UINT64_MAX / record.extent ||
        record.outputWidth * record.extent > UINT64_MAX - validation.output ||
        record.sourceSpan > UINT64_MAX / record.extent ||
        record.sourceSpan * record.extent >
            UINT64_MAX - validation.sourceCursor)
      return PlanValidation::Invalid;
    validation.output += record.outputWidth * record.extent;
    validation.sourceCursor += record.sourceSpan * record.extent;
    uint64_t bodyStart = frame.index;
    frame.index = bodyEnd;
    if (depth == OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_DEPTH)
      return PlanValidation::Invalid;
    if (depth + 1 >= capacity)
      return PlanValidation::NeedsDepth;
    ++depth;
    stack[depth].index = bodyStart;
    stack[depth].end = bodyEnd;
    stack[depth].state.validation = {record.sourceSpan, 0, record.outputWidth,
                                     0};
  }
}

OBELISK_RT_FEATURE_HELPER void
executePlan(const uint8_t *records, uint64_t recordCount,
            const uint8_t *inputValue, const uint8_t *inputUnknown,
            uint8_t *outputValue, uint8_t *outputUnknown, uint64_t outputWidth,
            bool outputFourState, PlanFrame *stack) {
  uint64_t depth = 0;
  uint64_t cursor = outputWidth;
  stack[0].index = 0;
  stack[0].end = recordCount;
  stack[0].state.execution = {0, 0, 1, 0, 0};
  while (true) {
    PlanFrame &frame = stack[depth];
    auto &execution = frame.state.execution;
    if (frame.index == frame.end) {
      ++execution.repeat;
      if (execution.repeat < execution.repeatCount) {
        frame.index = execution.repeatBase;
        execution.sourceBase += execution.repeatStride;
        continue;
      }
      if (depth == 0)
        return;
      --depth;
      continue;
    }
    PlanRecord record = readRecord(records, frame.index++);
    if (record.opcode == OBELISK_RT_AGGREGATE_BITSTREAM_COPY) {
      cursor -= record.extent;
      copyBits(outputValue, cursor, inputValue, inputUnknown,
               execution.sourceBase + record.sourceOffset, record.extent,
               outputFourState, outputUnknown);
      continue;
    }
    uint64_t bodyStart = frame.index;
    frame.index += record.bodyRecords;
    ++depth;
    stack[depth].index = bodyStart;
    stack[depth].end = bodyStart + record.bodyRecords;
    stack[depth].state.execution = {execution.sourceBase + record.sourceOffset,
                                    0, record.extent, bodyStart, record.stride};
  }
}

OBELISK_RT_FEATURE_HELPER bool checkedRange(const void *pointer, uint64_t size,
                                            uintptr_t &begin, uintptr_t &end) {
  if (!pointer || size > UINTPTR_MAX)
    return false;
  begin = reinterpret_cast<uintptr_t>(pointer);
  if (size > UINTPTR_MAX - begin)
    return false;
  end = begin + static_cast<uintptr_t>(size);
  return true;
}

OBELISK_RT_FEATURE_HELPER bool overlaps(uintptr_t leftBegin, uintptr_t leftEnd,
                                        uintptr_t rightBegin,
                                        uintptr_t rightEnd) {
  return leftBegin < rightEnd && rightBegin < leftEnd;
}

} // namespace

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_v1_aggregate_export_bitstream(
    const void *inputValue, const void *inputUnknown, uint64_t inputPlaneSize,
    uint64_t inputBitWidth, uint32_t inputFourState, void *outValue,
    void *outUnknown, uint64_t outputPlaneSize, uint64_t outputBitWidth,
    uint32_t outputFourState, const void *plan, uint64_t planSize) {
  constexpr uint64_t headerSize =
      OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_HEADER_WORDS * 8;
  constexpr uint64_t recordSize =
      OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_RECORD_WORDS * 8;
  if (!inputValue || !outValue || !plan || inputFourState > 1 ||
      outputFourState > 1 || (inputFourState && !inputUnknown) ||
      (outputFourState && !outUnknown) || inputBitWidth == 0 ||
      outputBitWidth == 0 || inputPlaneSize > SIZE_MAX ||
      outputPlaneSize > SIZE_MAX || planSize > SIZE_MAX ||
      inputPlaneSize < inputBitWidth / 8 + ((inputBitWidth & 7) != 0) ||
      outputPlaneSize < outputBitWidth / 8 + ((outputBitWidth & 7) != 0) ||
      planSize < headerSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  uintptr_t inputValueBegin, inputValueEnd, inputUnknownBegin = 0,
                                            inputUnknownEnd = 0;
  uintptr_t outputValueBegin, outputValueEnd, outputUnknownBegin = 0,
                                              outputUnknownEnd = 0;
  uintptr_t planBegin, planEnd;
  if (!checkedRange(inputValue, inputPlaneSize, inputValueBegin,
                    inputValueEnd) ||
      (inputFourState && !checkedRange(inputUnknown, inputPlaneSize,
                                       inputUnknownBegin, inputUnknownEnd)) ||
      !checkedRange(outValue, outputPlaneSize, outputValueBegin,
                    outputValueEnd) ||
      (outputFourState &&
       !checkedRange(outUnknown, outputPlaneSize, outputUnknownBegin,
                     outputUnknownEnd)) ||
      !checkedRange(plan, planSize, planBegin, planEnd))
    return OBELISK_RT_INVALID_ARGUMENT;
  auto outputOverlapsInput = [&](uintptr_t begin, uintptr_t end) {
    return overlaps(begin, end, inputValueBegin, inputValueEnd) ||
           (inputFourState &&
            overlaps(begin, end, inputUnknownBegin, inputUnknownEnd)) ||
           overlaps(begin, end, planBegin, planEnd);
  };
  if (outputOverlapsInput(outputValueBegin, outputValueEnd) ||
      (outputFourState &&
       (outputOverlapsInput(outputUnknownBegin, outputUnknownEnd) ||
        overlaps(outputValueBegin, outputValueEnd, outputUnknownBegin,
                 outputUnknownEnd))))
    return OBELISK_RT_INVALID_ARGUMENT;
  const auto *bytes = static_cast<const uint8_t *>(plan);
  uint64_t identity = readPlan64(bytes);
  uint64_t recordCount = readPlan64(bytes + 8);
  uint64_t sourceSpan = readPlan64(bytes + 16);
  uint64_t streamWidth = readPlan64(bytes + 24);
  if (static_cast<uint32_t>(identity) !=
          OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAGIC ||
      static_cast<uint32_t>(identity >> 32) !=
          OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_VERSION ||
      recordCount == 0 ||
      recordCount > OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_RECORDS ||
      recordCount > (UINT64_MAX - headerSize) / recordSize ||
      planSize != headerSize + recordCount * recordSize ||
      sourceSpan != inputBitWidth || streamWidth != outputBitWidth ||
      sourceSpan != streamWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  const uint8_t *records = bytes + headerSize;
  constexpr size_t inlineDepth = 16;
  std::array<PlanFrame, inlineDepth> inlineFrames;
  std::unique_ptr<PlanFrame[]> deepFrames;
  PlanFrame *frames = inlineFrames.data();
  uint64_t frameCapacity = inlineFrames.size();
  PlanValidation validation = validatePlan(records, recordCount, sourceSpan,
                                           streamWidth, frames, frameCapacity);
  if (validation == PlanValidation::NeedsDepth) {
    frameCapacity = OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAX_DEPTH + 1;
    deepFrames.reset(new (std::nothrow) PlanFrame[frameCapacity]);
    if (!deepFrames)
      return OBELISK_RT_OUT_OF_MEMORY;
    frames = deepFrames.get();
    validation = validatePlan(records, recordCount, sourceSpan, streamWidth,
                              frames, frameCapacity);
  }
  if (validation != PlanValidation::Valid)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(outValue, 0, static_cast<size_t>(outputPlaneSize));
  if (outputFourState)
    std::memset(outUnknown, 0, static_cast<size_t>(outputPlaneSize));
  executePlan(records, recordCount, static_cast<const uint8_t *>(inputValue),
              inputFourState ? static_cast<const uint8_t *>(inputUnknown)
                             : nullptr,
              static_cast<uint8_t *>(outValue),
              outputFourState ? static_cast<uint8_t *>(outUnknown) : nullptr,
              outputBitWidth, outputFourState != 0, frames);
  return OBELISK_RT_OK;
}
