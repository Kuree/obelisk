#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t __obelisk_state_unknown[];
extern void *__obelisk_eval_function_route_v1_0;
void __obelisk_eval_route_promotion_scan_v1();
}

static bool certified(unsigned bit) {
  return (bit >= 11 && bit < 140) || (bit >= 168 && bit < 297) ||
         (bit >= 304 && bit < 368);
}

int main() {
  constexpr unsigned bits = 368;
  constexpr unsigned bytes = bits / 8;
  std::memset(__obelisk_state_unknown, 0, bytes);
  __obelisk_eval_route_promotion_scan_v1();
  void *known = __obelisk_eval_function_route_v1_0;
  assert(known);
  auto check = [&] {
    bool expectedKnown = true;
    for (unsigned bit = 0; bit != bits; ++bit)
      if (certified(bit) &&
          (__obelisk_state_unknown[bit / 8] & (1u << (bit % 8))))
        expectedKnown = false;
    __obelisk_eval_route_promotion_scan_v1();
    assert((__obelisk_eval_function_route_v1_0 == known) == expectedKnown);
  };
  // Every bit individually: boundaries, overlapping views, full words, and
  // holes must agree with the scalar union-of-bit-ranges reference.
  for (unsigned bit = 0; bit != bits; ++bit) {
    __obelisk_state_unknown[bit / 8] |= 1u << (bit % 8);
    check();
    __obelisk_state_unknown[bit / 8] = 0;
  }
  // All uncertified bits are unknown simultaneously, including both partial
  // boundary bytes. They must not prevent promotion or be cleared by a scan.
  for (unsigned bit = 0; bit != bits; ++bit)
    if (!certified(bit))
      __obelisk_state_unknown[bit / 8] |= 1u << (bit % 8);
  check();
  uint8_t original[bytes];
  std::memcpy(original, __obelisk_state_unknown, bytes);
  for (unsigned bit = 0; bit != bits; ++bit) {
    if (!certified(bit))
      continue;
    __obelisk_state_unknown[bit / 8] |= 1u << (bit % 8);
    check();
    __obelisk_state_unknown[bit / 8] &= ~(1u << (bit % 8));
  }
  assert(std::memcmp(original, __obelisk_state_unknown, bytes) == 0);
  std::puts("promotion word scans passed");
}
