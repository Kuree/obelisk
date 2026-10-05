#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

extern "C" {
extern uint8_t __obelisk_state_value[], __obelisk_state_unknown[];
extern uint8_t __obelisk_eval_selected_variant_v1_0;
extern uint8_t __obelisk_eval_step_four_state_fallback_v1;
extern uint64_t __obelisk_eval_route_promotion_pending_v1[];
void __obelisk_eval_route_promotion_scan_v1();
void __obelisk_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
void __obelisk_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
bool __obelisk_eval_kernel_promotion_ready_v1(uint64_t);
int execute(void *) asm("__obelisk_direct_fragment_2_5.__obelisk_execute");
}

static bool certified(unsigned bit) {
  return (bit >= 11 && bit < 20) || (bit >= 23 && bit < 32) ||
         (bit >= 40 && bit < 43);
}
static void setBit(uint8_t *plane, unsigned bit, bool value) {
  uint8_t mask = 1u << (bit % 8);
  plane[bit / 8] = (plane[bit / 8] & ~mask) | (value ? mask : 0);
}
static void initialize() {
  std::memset(__obelisk_state_value, 0, 6);
  std::memset(__obelisk_state_unknown, 0, 6);
  unsigned expected = 0;
  for (unsigned cell = 0; cell < 9; ++cell) {
    bool diagonal = cell / 3 == cell % 3;
    unsigned value = (cell + 1) & 7;
    if (!diagonal)
      expected ^= value;
    for (unsigned bit = 0; bit < 3; ++bit) {
      setBit(__obelisk_state_value, 8 + cell * 3 + bit, value & (1u << bit));
      setBit(__obelisk_state_unknown, 8 + cell * 3 + bit, diagonal);
    }
  }
  __obelisk_eval_promotion_invalidate_range_v1(0, 48);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_selected_variant_v1_0);
  assert(__obelisk_eval_kernel_promotion_ready_v1(0));
  uint8_t before[6];
  std::memcpy(before, __obelisk_state_unknown, 6);
  __obelisk_eval_step_four_state_fallback_v1 = 0;
  assert(execute(nullptr) == 0);
  assert(!__obelisk_eval_step_four_state_fallback_v1);
  assert((__obelisk_state_value[5] & 7) == expected);
  assert(std::memcmp(before, __obelisk_state_unknown, 6) == 0);
}

int main() {
  // Every checked bit revokes promotion; every hole (including padding and
  // all three skipped diagonal elements) leaves it usable. Neither scanning
  // nor invalidation may rewrite the canonical unknown plane.
  for (unsigned bit = 0; bit < 48; ++bit) {
    initialize();
    uint8_t before[6];
    setBit(__obelisk_state_unknown, bit, true);
    std::memcpy(before, __obelisk_state_unknown, 6);
    __obelisk_eval_promotion_invalidate_range_v1(bit, 1);
    assert(bool(__obelisk_eval_route_promotion_pending_v1[0]) ==
           certified(bit));
    __obelisk_eval_route_promotion_scan_v1();
    assert(bool(__obelisk_eval_selected_variant_v1_0) == !certified(bit));
    assert(__obelisk_eval_kernel_promotion_ready_v1(0) == !certified(bit));
    assert(std::memcmp(before, __obelisk_state_unknown, 6) == 0);
    if (!certified(bit)) {
      __obelisk_eval_step_four_state_fallback_v1 = 0;
      assert(execute(nullptr) == 0);
      assert(!__obelisk_eval_step_four_state_fallback_v1);
      assert(std::memcmp(before, __obelisk_state_unknown, 6) == 0);
    }
  }
  // Reachable X and Z must execute the original body and propagate unknown
  // bits through XOR. Clearing only that input does not certify an output
  // that still contains X: a canonical activation first restores its value.
  for (bool z : {false, true}) {
    initialize();
    setBit(__obelisk_state_unknown, 11, true);
    setBit(__obelisk_state_value, 11, z);
    __obelisk_eval_promotion_invalidate_range_v1(11, 1);
    __obelisk_eval_route_promotion_scan_v1();
    assert(!__obelisk_eval_selected_variant_v1_0);
    __obelisk_eval_step_four_state_fallback_v1 = 0;
    assert(execute(nullptr) == 0);
    assert(__obelisk_eval_step_four_state_fallback_v1);
    assert(__obelisk_state_unknown[5] & 1);
    setBit(__obelisk_state_unknown, 11, false);
    __obelisk_eval_promotion_recheck_range_v1(11, 1);
    __obelisk_eval_route_promotion_scan_v1();
    assert(!__obelisk_eval_selected_variant_v1_0);
    assert(execute(nullptr) == 0);
    assert((__obelisk_state_unknown[5] & 7) == 0);
    __obelisk_eval_route_promotion_scan_v1();
    assert(__obelisk_eval_selected_variant_v1_0);
    assert(__obelisk_eval_kernel_promotion_ready_v1(0));
  }
  std::puts("access footprint promotion passed");
}
