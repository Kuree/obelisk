//===- DesignBytecodeImage.h - Validated bytecode image model ---*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_DESIGNBYTECODEIMAGE_H
#define OBELISK_RUNTIME_LIB_DESIGNBYTECODEIMAGE_H

#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include <cstdint>
#include <cstring>
#include <utility>

namespace obelisk::designbytecode {

constexpr uint32_t kInvalidRegister = UINT32_MAX;
constexpr uint32_t kNetStateDescriptor = UINT32_MAX - 1;
constexpr uint32_t kDriverStateDescriptor = UINT32_MAX;
constexpr uint32_t kAutomaticHandleKind = UINT32_C(1) << 30;
constexpr uint32_t kLocalHandleKind = UINT32_C(1) << 31;
constexpr int64_t kInvalidHandleStart = INT64_MIN;

inline bool decodeAutomaticHandle(uint64_t handle, uint32_t &id,
                                  int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_AUTOMATIC)
    return false;
  id = decoded.id;
  offset = decoded.offset;
  return true;
}

inline bool decodeStaticHandle(uint64_t handle, uint32_t &id, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC)
    return false;
  id = decoded.id;
  offset = decoded.offset;
  return true;
}

inline bool decodeGlobalHandle(uint64_t handle, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL)
    return false;
  offset = decoded.offset;
  return true;
}

inline uint64_t encodeGlobalHandle(int64_t offset) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_GLOBAL, 0,
                                         offset);
}
inline uint64_t encodeAutomaticHandle(uint32_t id, int64_t offset) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_AUTOMATIC, id,
                                         offset);
}
inline uint64_t encodeStaticHandle(uint32_t id, int64_t offset) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, id,
                                         offset);
}

inline bool isDynamicEventStableHandle(uint64_t stable) {
  return stable != OBELISK_RT_STABLE_HANDLE_DYNAMIC_EVENT_TAG &&
         (stable & OBELISK_RT_STABLE_HANDLE_DYNAMIC_EVENT_TAG) != 0 &&
         (stable & OBELISK_RT_STABLE_HANDLE_TAG_MASK) == 0;
}

inline bool isDynamicEventHandle(uint32_t kind, uint64_t stable) {
  uint32_t descriptorKind =
      kind & ~(kLocalHandleKind | kAutomaticHandleKind);
  return descriptorKind == OBELISK_RT_DESCRIPTOR_EVENT &&
         isDynamicEventStableHandle(stable);
}

inline bool encodeCanonicalHandle(const uint8_t *address, uint64_t &stable) {
  uint32_t kind = 0;
  int64_t start = kInvalidHandleStart;
  std::memcpy(&kind, address, sizeof(kind));
  std::memcpy(&start, address + 16, sizeof(start));
  if ((kind & kLocalHandleKind) != 0)
    return false;
  if (start == kInvalidHandleStart) {
    stable = UINT64_MAX;
    return true;
  }
  uint64_t raw = static_cast<uint64_t>(start);
  if (isDynamicEventHandle(kind, raw)) {
    stable = raw;
    return true;
  }
  stable = encodeGlobalHandle(start);
  uint64_t base = 0;
  std::memcpy(&base, address + 8, sizeof(base));
  uint32_t id = 0;
  int64_t begin = 0;
  if ((kind & kAutomaticHandleKind) != 0) {
    if (!decodeAutomaticHandle(base, id, begin))
      return false;
    stable = encodeAutomaticHandle(id, start);
  } else if (decodeStaticHandle(base, id, begin)) {
    stable = encodeStaticHandle(id, start);
  }
  return stable != UINT64_MAX;
}

struct Image {
  const uint8_t *data = nullptr;
  uint64_t size = 0;
  uint64_t functions = 0, functionCount = 0;
  uint64_t layouts = 0, layoutCount = 0;
  uint64_t code = 0, instructionCount = 0;
  uint64_t operands = 0, operandCount = 0;
  uint64_t constants = 0, constantSize = 0;
  uint64_t continuations = 0, continuationCount = 0;
  uint64_t intrinsics = 0, intrinsicCount = 0;
  uint64_t sites = 0, siteCount = 0;
  uint64_t stateDescriptors = 0, stateDescriptorCount = 0;
  uint64_t connectivity = 0, connectivityCount = 0;
  uint64_t stateBitCount = 0;
};

struct Function {
  uint64_t id = 0, initialScheduleRank = UINT32_MAX;
  uint64_t firstInstruction = 0, instructionCount = 0;
  uint64_t firstLayout = 0, layoutCount = 0;
  uint32_t argumentCount = 0, resultCount = 0;
  uint64_t scratchSize = 0, scratchAlignment = 0;
  uint64_t firstContinuation = 0, continuationCount = 0;
  uint64_t flags = 0;
};

struct Layout {
  uint8_t kind = 0, flags = 0;
  uint32_t width = 0;
  uint64_t offset = 0, size = 0, auxiliary = 0;
};

struct Instruction {
  uint16_t opcode = 0, flags = 0;
  uint32_t destination = 0, source0 = 0, source1 = 0, source2 = 0;
  uint32_t auxiliary = 0;
  uint64_t immediate = 0;
};

struct Continuation {
  uint32_t function = 0, id = 0;
  uint64_t instruction = 0;
  uint32_t scheduleRank = UINT32_MAX, reserved = 0;
};

struct IntrinsicSignature {
  uint32_t id = 0, inputCount = 0, outputCount = 0, flags = 0;
};
struct IntrinsicSite {
  uint32_t intrinsic = 0, firstOperand = 0, inputCount = 0, outputCount = 0;
};
struct CaptureRecord {
  uint32_t function = 0, argument = 0;
  uint64_t valueOffset = 0, unknownOffset = 0, planeSize = 0;
};
struct ConnectivityRecord {
  uint64_t lhsOffset = 0, rhsOffset = 0, width = 0;
  uint8_t lhsResolution = 0, rhsResolution = 0, flags = 0, reserved = 0;
  uint32_t tailReserved = 0;
};

uint32_t functionHomeRegion(const Function &function);
uint32_t read32(const uint8_t *data);
uint64_t read64(const uint8_t *data);
bool parseImage(const obelisk_rt_design_bytecode_entry_v1 &entry, Image &image);
bool validateImage(const Image &image);
Function functionAt(const Image &image, uint32_t index);
Continuation continuationAt(const Image &image, uint64_t index);
Layout layoutAt(const Image &image, const Function &function, uint32_t index);
Instruction instructionAt(const Image &image, uint64_t index);
IntrinsicSignature intrinsicAt(const Image &image, uint32_t index);
IntrinsicSite siteAt(const Image &image, uint32_t index);
CaptureRecord captureAt(const Image &image, uint64_t index);
ConnectivityRecord connectivityAt(const Image &image, uint64_t index);
std::pair<uint32_t, uint32_t> operandAt(const Image &image, uint64_t index);
bool compatible(const Layout &left, const Layout &right);
bool bitcastCompatible(const Layout &left, const Layout &right);
bool validRegister(const Function &function, uint32_t index);
bool validMap(const Image &image, const Function &source,
              const Function &destination, uint64_t first, uint64_t count);
bool validIntrinsic(const Image &image, const Function &function,
                    uint32_t siteIndex);
bool loadValidatedImage(const obelisk_rt_design_bytecode_entry_v1 &entry,
                        obelisk_rt_context *context, Image &image);

} // namespace obelisk::designbytecode

#endif // OBELISK_RUNTIME_LIB_DESIGNBYTECODEIMAGE_H
