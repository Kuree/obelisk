//===- ContainerBitstream.cpp - Feature-local bit-stream export -----------===//

#include "ContainerStorageInternal.h"
#include "RuntimeInternal.h"

#include <cstring>

namespace {

using obelisk::runtime_detail::BufferHeader;
using obelisk::runtime_detail::ContainerHeader;

OBELISK_RT_FEATURE_HELPER void
copyBits(uint8_t *destination, uint64_t destinationOffset,
         const uint8_t *source, const uint8_t *sourceUnknown, uint64_t width,
         bool outputFourState, uint8_t *outputUnknown) {
  if (((destinationOffset | width) & 7) == 0) {
    uint8_t *output = destination + destinationOffset / 8;
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
    uint8_t sourceMask = static_cast<uint8_t>(1u << (bit & 7));
    uint64_t destinationBit = destinationOffset + bit;
    uint8_t destinationMask = static_cast<uint8_t>(1u << (destinationBit & 7));
    uint8_t &output = destination[destinationBit / 8];
    bool unknown = sourceUnknown && (sourceUnknown[bit / 8] & sourceMask);
    if ((source[bit / 8] & sourceMask) && (!unknown || outputFourState))
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
             sourceUnknown, request.elementWidth, request.fourState != 0,
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
