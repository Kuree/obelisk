//===- ProcessPacking.h - Packed plane access and handle decoding -*- C++ -*-===//
//
// Bit-plane accessors and stable-handle decoding shared by the process and
// scheduler translation units.  These are leaf helpers on the hot path, so
// they are inline rather than a separate translation unit.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_RUNTIME_LIB_PROCESSPACKING_H
#define OBELISK_RUNTIME_LIB_PROCESSPACKING_H

#include "obelisk/Runtime/StableHandle.h"

#include <cstdint>
#include <cstring>
#include <vector>

inline bool decodeNativeAutomatic(uint64_t handle, uint32_t &id, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_AUTOMATIC)
    return false;
  id = decoded.id;
  offset = decoded.offset;
  return true;
}

inline bool decodeNativeGlobal(uint64_t handle, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL)
    return false;
  offset = decoded.offset;
  return true;
}

inline bool decodeNativeStatic(uint64_t handle, uint32_t &id, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC)
    return false;
  id = decoded.id;
  offset = decoded.offset;
  return true;
}

inline bool addHandleOffset(int64_t base, uint64_t offset, int64_t &result) {
  if (offset > static_cast<uint64_t>(INT64_MAX) ||
      base > INT64_MAX - static_cast<int64_t>(offset))
    return false;
  result = base + static_cast<int64_t>(offset);
  return true;
}

inline uint64_t nativeHandleOffset(uint64_t handle, int64_t amount) {
  return obelisk_rt_stable_handle_offset(handle, amount);
}

inline uint64_t packedWidthMask(uint64_t bitWidth) {
  return bitWidth == 64 ? UINT64_MAX : (uint64_t{1} << bitWidth) - 1;
}

inline uint64_t loadPackedBits(const std::vector<uint64_t> &plane, uint64_t bitOffset,
                        uint64_t bitWidth) {
  uint64_t limb = bitOffset / 64;
  uint32_t shift = bitOffset % 64;
  uint64_t value = plane[limb] >> shift;
  if (shift != 0 && bitWidth > 64 - shift)
    value |= plane[limb + 1] << (64 - shift);
  return value & packedWidthMask(bitWidth);
}

inline uint64_t loadPackedBytes(const uint8_t *plane, uint64_t bitOffset,
                         uint64_t bitWidth) {
  uint64_t firstByte = bitOffset / 8;
  uint32_t shift = bitOffset % 8;
  uint64_t byteCount = (shift + bitWidth + 7) / 8;
  if (byteCount == 1)
    return (plane[firstByte] >> shift) & packedWidthMask(bitWidth);
  if (shift == 0) {
    uint64_t value = 0;
    switch (bitWidth) {
    case 8:
      return plane[firstByte];
    case 16: {
      uint16_t loaded;
      std::memcpy(&loaded, plane + firstByte, sizeof(loaded));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      loaded = __builtin_bswap16(loaded);
#endif
      return loaded;
    }
    case 32: {
      uint32_t loaded;
      std::memcpy(&loaded, plane + firstByte, sizeof(loaded));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      loaded = __builtin_bswap32(loaded);
#endif
      return loaded;
    }
    case 64:
      std::memcpy(&value, plane + firstByte, sizeof(value));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      value = __builtin_bswap64(value);
#endif
      return value;
    default:
      break;
    }
  }
  unsigned __int128 bits = 0;
  for (uint64_t byte = 0; byte != byteCount; ++byte)
    bits |= static_cast<unsigned __int128>(plane[firstByte + byte])
            << (byte * 8);
  return static_cast<uint64_t>(bits >> shift) & packedWidthMask(bitWidth);
}

inline void storePackedBits(std::vector<uint64_t> &plane, uint64_t bitOffset,
                     uint64_t bitWidth, uint64_t value) {
  uint64_t limb = bitOffset / 64;
  uint32_t shift = bitOffset % 64;
  uint64_t mask = packedWidthMask(bitWidth);
  value &= mask;
  uint64_t lowMask = mask << shift;
  plane[limb] = (plane[limb] & ~lowMask) | (value << shift);
  if (shift != 0 && bitWidth > 64 - shift) {
    uint32_t lowBits = 64 - shift;
    uint64_t highMask = mask >> lowBits;
    plane[limb + 1] = (plane[limb + 1] & ~highMask) | (value >> lowBits);
  }
}

inline void storePackedBytes(uint8_t *plane, uint64_t bitOffset, uint64_t bitWidth,
                      uint64_t value) {
  uint64_t firstByte = bitOffset / 8;
  uint32_t shift = bitOffset % 8;
  uint64_t byteCount = (shift + bitWidth + 7) / 8;
  if (byteCount == 1) {
    uint8_t mask = static_cast<uint8_t>(packedWidthMask(bitWidth) << shift);
    plane[firstByte] = static_cast<uint8_t>(
        (plane[firstByte] & ~mask) | ((value << shift) & mask));
    return;
  }
  if (shift == 0) {
    switch (bitWidth) {
    case 8:
      plane[firstByte] = static_cast<uint8_t>(value);
      return;
    case 16: {
      uint16_t stored = static_cast<uint16_t>(value);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      stored = __builtin_bswap16(stored);
#endif
      std::memcpy(plane + firstByte, &stored, sizeof(stored));
      return;
    }
    case 32: {
      uint32_t stored = static_cast<uint32_t>(value);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      stored = __builtin_bswap32(stored);
#endif
      std::memcpy(plane + firstByte, &stored, sizeof(stored));
      return;
    }
    case 64: {
      uint64_t stored = value;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      stored = __builtin_bswap64(stored);
#endif
      std::memcpy(plane + firstByte, &stored, sizeof(stored));
      return;
    }
    default:
      break;
    }
  }
  unsigned __int128 bits = 0;
  for (uint64_t byte = 0; byte != byteCount; ++byte)
    bits |= static_cast<unsigned __int128>(plane[firstByte + byte])
            << (byte * 8);
  unsigned __int128 mask = (static_cast<unsigned __int128>(1) << bitWidth) - 1;
  unsigned __int128 positionedMask = mask << shift;
  bits = (bits & ~positionedMask) |
         ((static_cast<unsigned __int128>(value) & mask) << shift);
  for (uint64_t byte = 0; byte != byteCount; ++byte)
    plane[firstByte + byte] = static_cast<uint8_t>(bits >> (byte * 8));
}

#endif // OBELISK_RUNTIME_LIB_PROCESSPACKING_H
