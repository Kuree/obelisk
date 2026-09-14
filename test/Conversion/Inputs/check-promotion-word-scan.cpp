#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t __obelisk_state_unknown[];
extern void *__obelisk_eval_function_route_v1_0;
extern void *__obelisk_eval_function_route_v1_1;
extern uint8_t __obelisk_eval_kernel_promotion_latched_v1[];
extern uint64_t __obelisk_eval_promotion_pending_mask_v1[];
bool __obelisk_eval_kernel_promotion_ready_v1_0();
bool __obelisk_eval_kernel_promotion_ready_v1_1();
void __obelisk_eval_route_promotion_scan_v1();
void __obelisk_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
}

static bool certified(unsigned bit) {
  return (bit >= 11 && bit < 140) || (bit >= 168 && bit < 297) ||
         (bit >= 304 && bit < 368);
}

int main() {
  constexpr unsigned bits = 408;
  constexpr unsigned bytes = bits / 8;
  std::memset(__obelisk_state_unknown, 0, bytes);
  __obelisk_eval_route_promotion_scan_v1();
  void *known = __obelisk_eval_function_route_v1_0;
  void *otherKnown = __obelisk_eval_function_route_v1_1;
  assert(known && otherKnown);
  // Exercise the emitted reverse index through its real runtime helper.
  // Every certified bit invalidates this route, while holes and partial-byte
  // neighbors preserve it. Invalidation must never alter canonical X/Z state.
  for (unsigned bit = 0; bit != bits; ++bit) {
    __obelisk_eval_route_promotion_scan_v1();
    assert(__obelisk_eval_function_route_v1_0 == known);
    assert(__obelisk_eval_function_route_v1_1 == otherKnown);
    assert(__obelisk_eval_kernel_promotion_ready_v1_0());
    assert(__obelisk_eval_kernel_promotion_ready_v1_1());
    assert(__obelisk_eval_promotion_pending_mask_v1[0] == 0);
    __obelisk_eval_promotion_invalidate_range_v1(bit, 1);
    assert((__obelisk_eval_function_route_v1_0 != known) == certified(bit));
    bool otherAffected = bit >= 376;
    assert((__obelisk_eval_function_route_v1_1 != otherKnown) == otherAffected);
    assert(__obelisk_eval_kernel_promotion_latched_v1[0] == !otherAffected);
    assert(__obelisk_eval_kernel_promotion_latched_v1[1] == !certified(bit));
    assert(__obelisk_eval_promotion_pending_mask_v1[0] ==
           (unsigned(otherAffected) | (unsigned(certified(bit)) << 1)));
    for (unsigned byte = 0; byte != bytes; ++byte)
      assert(__obelisk_state_unknown[byte] == 0);
  }
  __obelisk_eval_route_promotion_scan_v1();
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
