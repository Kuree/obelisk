#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

// asm labels are final symbol names; Mach-O prefixes C symbols with '_'.
#define TALOS_TEST_STRINGIFY_(text) #text
#define TALOS_TEST_STRINGIFY(text) TALOS_TEST_STRINGIFY_(text)
#define TALOS_TEST_SYMBOL(name)                                              \
  TALOS_TEST_STRINGIFY(__USER_LABEL_PREFIX__) name

extern "C" {
extern uint8_t __talos_state_value[], __talos_state_unknown[];
extern uint8_t __talos_eval_kernel_promotion_latched_v1[];
extern uint8_t __talos_eval_step_four_state_fallback_v1;
extern uint64_t __talos_eval_promotion_pending_mask_v1[];
void __talos_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
void __talos_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
bool __talos_eval_kernel_promotion_ready_v1(uint64_t);
int execute(void *) asm(
    TALOS_TEST_SYMBOL("__talos_direct_fragment_2_5.__talos_execute"));
}

static bool certified(unsigned bit) {
  return (bit >= 35 && bit < 44) || (bit >= 47 && bit < 56) ||
         (bit >= 64 && bit < 67);
}
static void setBit(uint8_t *plane, unsigned bit, bool value) {
  uint8_t mask = 1u << (bit % 8);
  plane[bit / 8] = (plane[bit / 8] & ~mask) | (value ? mask : 0);
}
static void initialize() {
  std::memset(__talos_state_value, 0, 9);
  std::memset(__talos_state_unknown, 0, 9);
  // Clock: bit 0; matrix: [32,59); output: [64,67).
  unsigned expected = 0;
  for (unsigned cell = 0; cell < 9; ++cell) {
    bool diagonal = cell / 3 == cell % 3;
    unsigned value = (cell + 1) & 7;
    if (!diagonal)
      expected ^= value;
    for (unsigned bit = 0; bit < 3; ++bit) {
      setBit(__talos_state_value, 32 + cell * 3 + bit, value & (1u << bit));
      setBit(__talos_state_unknown, 32 + cell * 3 + bit, diagonal);
    }
  }
  __talos_eval_promotion_invalidate_range_v1(0, 72);
  // Enter the route before its scheduled owner has run. It must refresh
  // the shared certificate on demand, without an eager route scan.
  assert(!__talos_eval_kernel_promotion_latched_v1[0]);
  uint8_t before[9];
  std::memcpy(before, __talos_state_unknown, 9);
  __talos_eval_step_four_state_fallback_v1 = 0;
  assert(execute(nullptr) == 0);
  assert(!__talos_eval_step_four_state_fallback_v1);
  assert(__talos_eval_kernel_promotion_latched_v1[0]);
  assert((__talos_state_value[8] & 7) == expected);
  assert(std::memcmp(before, __talos_state_unknown, 9) == 0);
}

int main() {
  // Every checked bit revokes promotion; every hole (including padding and
  // all three skipped diagonal elements) leaves it usable. Neither scanning
  // nor invalidation may rewrite the canonical unknown plane.
  for (unsigned bit = 0; bit < 72; ++bit) {
    initialize();
    uint8_t before[9];
    setBit(__talos_state_unknown, bit, true);
    std::memcpy(before, __talos_state_unknown, 9);
    __talos_eval_promotion_invalidate_range_v1(bit, 1);
    assert(bool(__talos_eval_promotion_pending_mask_v1[0]) == certified(bit));
    (void)__talos_eval_kernel_promotion_ready_v1(0);
    assert(bool(__talos_eval_kernel_promotion_latched_v1[0]) ==
           !certified(bit));
    assert(__talos_eval_kernel_promotion_ready_v1(0) == !certified(bit));
    assert(std::memcmp(before, __talos_state_unknown, 9) == 0);
    if (!certified(bit)) {
      __talos_eval_step_four_state_fallback_v1 = 0;
      assert(execute(nullptr) == 0);
      assert(!__talos_eval_step_four_state_fallback_v1);
      // The promoted body omits unknown-plane stores, including padding.
      assert(std::memcmp(before, __talos_state_unknown, 9) == 0);
    }
  }
  // Reachable X and Z must execute the original body and propagate unknown
  // bits through XOR. Clearing only that input does not certify an output
  // that still contains X: a canonical activation first restores its value.
  for (bool z : {false, true}) {
    initialize();
    setBit(__talos_state_unknown, 35, true);
    setBit(__talos_state_value, 35, z);
    __talos_eval_promotion_invalidate_range_v1(35, 1);
    (void)__talos_eval_kernel_promotion_ready_v1(0);
    assert(!__talos_eval_kernel_promotion_latched_v1[0]);
    __talos_eval_step_four_state_fallback_v1 = 0;
    assert(execute(nullptr) == 0);
    assert(__talos_eval_step_four_state_fallback_v1);
    assert(__talos_state_unknown[8] & 1);
    setBit(__talos_state_unknown, 35, false);
    __talos_eval_promotion_recheck_range_v1(35, 1);
    (void)__talos_eval_kernel_promotion_ready_v1(0);
    assert(!__talos_eval_kernel_promotion_latched_v1[0]);
    assert(execute(nullptr) == 0);
    assert((__talos_state_unknown[8] & 7) == 0);
    (void)__talos_eval_kernel_promotion_ready_v1(0);
    assert(__talos_eval_kernel_promotion_latched_v1[0]);
    assert(__talos_eval_kernel_promotion_ready_v1(0));
  }
  std::puts("access footprint promotion passed");
}
