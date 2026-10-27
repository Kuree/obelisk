#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t __talos_state_value[], __talos_state_unknown[];
void write3(void *, int32_t);
void write17(void *, int32_t);
void write33(void *, int32_t);
void write57(void *, int32_t);
}

int main() {
  // The fixture has 2769 logical bits: 347 bytes plus the eight-byte guard.
  // Its final 17-bit array element uses an i32 window touching guard byte 347.
  constexpr unsigned bytes = 355;
  struct Root {
    void (*write)(void *, int32_t);
    unsigned offset, width;
  };
  for (Root root : {Root{write3, 0, 3}, Root{write33, 80, 33},
                    Root{write57, 912, 57}, Root{write17, 2344, 17}}) {
    for (int32_t index : {INT32_MIN, -1, 0,  1,  2,  3,  4,  5,  6,        7,
                          8,         9,  10, 11, 12, 13, 14, 15, 16,       17,
                          18,        19, 20, 21, 22, 23, 24, 25, INT32_MAX}) {
      std::array<uint8_t, bytes> value, unknown;
      for (unsigned byte = 0; byte < bytes; ++byte) {
        value[byte] = uint8_t(byte * 37 + 0xa5);
        unknown[byte] = uint8_t(byte * 19 + 0x5a);
      }
      std::memcpy(__talos_state_value, value.data(), bytes);
      std::memcpy(__talos_state_unknown, unknown.data(), bytes);
      root.write(nullptr, index);
      if (index >= 0 && index < 25) {
        for (unsigned bit = 0; bit < root.width; ++bit) {
          unsigned absolute = root.offset + index * root.width + bit;
          uint8_t mask = 1u << (absolute % 8);
          value[absolute / 8] = (value[absolute / 8] & ~mask) |
                                (((uint64_t{5} >> bit) & 1) ? mask : 0);
          unknown[absolute / 8] = (unknown[absolute / 8] & ~mask) |
                                  (((uint64_t{6} >> bit) & 1) ? mask : 0);
        }
      }
      assert(std::memcmp(value.data(), __talos_state_value, bytes) == 0);
      assert(std::memcmp(unknown.data(), __talos_state_unknown, bytes) == 0);
    }
  }
  std::puts("packed window guards preserved");
}
