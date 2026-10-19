//===- GeneratedNBAStorage.h - Physical generated NBA records ---*- C++ -*-===//
#ifndef OBELISK_RUNTIME_LIB_GENERATEDNBASTORAGE_H
#define OBELISK_RUNTIME_LIB_GENERATEDNBASTORAGE_H

#include "obelisk/Runtime/Runtime.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// The execution descriptor selects this compact internal layout. Public DPI
// records and the ordinary four-state generated record are unchanged.
struct TwoStateNBAAccumulator {
  uint64_t value[4];
  uint64_t write_mask[4];
  uint32_t valid;
  uint32_t exec_region;
  uint64_t transient[4];
};
static_assert(sizeof(TwoStateNBAAccumulator) ==
              sizeof(obelisk_rt_generated_nba_accumulator_256) - 32);

template <typename T> class NBAFieldReference {
public:
  explicit NBAFieldReference(T *pointer) : pointer(pointer) {}
  operator T() const { return pointer ? *pointer : 0; }
  NBAFieldReference &operator=(T value) {
    if (pointer)
      *pointer = value;
    return *this;
  }
  NBAFieldReference &operator=(const NBAFieldReference &other) {
    return *this = T(other);
  }

private:
  T *pointer;
};

class NBAWords {
public:
  explicit NBAWords(uint64_t *pointer) : pointer(pointer) {}
  NBAFieldReference<uint64_t> operator[](size_t index) const {
    return NBAFieldReference<uint64_t>(pointer ? pointer + index : nullptr);
  }
  uint64_t *data() const { return pointer; }
  operator uint64_t *() const { return pointer; }
  uint64_t *begin() const { return pointer; }
  uint64_t *end() const { return pointer ? pointer + 4 : nullptr; }

private:
  uint64_t *pointer;
};

// Non-owning view of either generated layout, including an implicit zero
// unknown field. No shadow record is allocated when selecting two-state.
class GeneratedNBAStorage {
  template <typename T> static T *field(void *base, size_t offset) {
    return base ? reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset)
                : nullptr;
  }
  static constexpr size_t select(bool twoState, size_t compact, size_t full) {
    return twoState ? compact : full;
  }

public:
  GeneratedNBAStorage(obelisk_rt_generated_nba_accumulator_256 *base,
                      bool twoState)
      : value(field<uint64_t>(base, 0)),
        unknown(twoState ? nullptr
                         : field<uint64_t>(
                               base,
                               offsetof(obelisk_rt_generated_nba_accumulator_256,
                                        unknown))),
        write_mask(field<uint64_t>(
            base, select(twoState, offsetof(TwoStateNBAAccumulator, write_mask),
                         offsetof(obelisk_rt_generated_nba_accumulator_256,
                                  write_mask)))),
        valid(field<uint32_t>(
            base, select(twoState, offsetof(TwoStateNBAAccumulator, valid),
                         offsetof(obelisk_rt_generated_nba_accumulator_256,
                                  valid)))),
        exec_region(field<uint32_t>(
            base,
            select(twoState, offsetof(TwoStateNBAAccumulator, exec_region),
                   offsetof(obelisk_rt_generated_nba_accumulator_256,
                            exec_region)))),
        transient(field<uint64_t>(
            base, select(twoState, offsetof(TwoStateNBAAccumulator, transient),
                         offsetof(obelisk_rt_generated_nba_accumulator_256,
                                  transient)))),
        base(base),
        bytes(twoState ? sizeof(TwoStateNBAAccumulator) : sizeof(*base)) {}
  explicit operator bool() const { return base; }
  GeneratedNBAStorage *operator->() { return this; }
  const GeneratedNBAStorage *operator->() const { return this; }
  GeneratedNBAStorage &operator*() { return *this; }
  const GeneratedNBAStorage &operator*() const { return *this; }
  void clear() {
    if (base)
      std::memset(base, 0, bytes);
  }

  NBAWords value, unknown, write_mask;
  NBAFieldReference<uint32_t> valid, exec_region;
  NBAWords transient;

private:
  void *base;
  size_t bytes;
};
#endif
