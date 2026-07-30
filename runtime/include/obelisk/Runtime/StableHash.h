//===- StableHash.h - Shared deterministic hash format ----------*- C -*-===//
//
// Header-only FNV-1a primitives used by compiler-emitted metadata and the
// runtime readers that validate it. These define a serialization contract,
// not a general-purpose hash table algorithm.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_RUNTIME_STABLEHASH_H
#define OBELISK_RUNTIME_STABLEHASH_H

#include <stddef.h>
#include <stdint.h>

#define OBELISK_STABLE_HASH_OFFSET_BASIS UINT64_C(14695981039346656037)
#define OBELISK_STABLE_HASH_PRIME UINT64_C(1099511628211)

static inline uint64_t obelisk_stable_hash_append_byte(uint64_t hash,
                                                       uint8_t byte) {
  return (hash ^ byte) * OBELISK_STABLE_HASH_PRIME;
}

static inline uint64_t obelisk_stable_hash_append(uint64_t hash,
                                                  const void *data,
                                                  size_t size) {
  const uint8_t *bytes = (const uint8_t *)data;
  for (size_t index = 0; index != size; ++index)
    hash = obelisk_stable_hash_append_byte(hash, bytes[index]);
  return hash;
}

/// Append the low `byte_count` bytes of an integer in little-endian order.
static inline uint64_t obelisk_stable_hash_append_uint_le(
    uint64_t hash, uint64_t value, unsigned byte_count) {
  for (unsigned index = 0; index != byte_count; ++index)
    hash = obelisk_stable_hash_append_byte(
        hash, (uint8_t)(value >> (index * 8)));
  return hash;
}

static inline uint64_t obelisk_stable_hash(const void *data, size_t size) {
  return obelisk_stable_hash_append(OBELISK_STABLE_HASH_OFFSET_BASIS, data,
                                    size);
}

#endif // OBELISK_RUNTIME_STABLEHASH_H
