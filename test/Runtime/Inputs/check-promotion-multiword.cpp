#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t __obelisk_state_unknown[];
extern uint8_t __obelisk_eval_kernel_promotion_latched_v1[];
extern uint64_t __obelisk_eval_promotion_pending_mask_v1[];
bool __obelisk_eval_kernel_promotion_ready_v1(uint64_t);
extern uint8_t __obelisk_eval_selected_variant_v1_0;
extern uint8_t __obelisk_eval_selected_variant_v1_63;
extern uint8_t __obelisk_eval_selected_variant_v1_64;
extern uint64_t __obelisk_eval_route_promotion_pending_v1[];
extern uint8_t __obelisk_eval_route_promotion_dirty_v1;
int __obelisk_eval_route_promotion_boundary_v1(int);
void __obelisk_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
void __obelisk_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
}

int main() {
  // The fixture has one byte-aligned clock followed by 65 packed data bits.
  // Each writer's proof reads that clock and only its own destination bit.
  std::memset(__obelisk_state_unknown, 0, 10);
  auto &pending = __obelisk_eval_route_promotion_pending_v1;
  auto boundary = [] {
    assert(__obelisk_eval_route_promotion_boundary_v1(0) == 0);
  };
  auto change = [](unsigned bit, bool unknown) {
    uint8_t mask = 1u << (bit % 8);
    if (unknown) {
      __obelisk_state_unknown[bit / 8] |= mask;
      __obelisk_eval_promotion_invalidate_range_v1(bit, 1);
    } else {
      __obelisk_state_unknown[bit / 8] &= ~mask;
      __obelisk_eval_promotion_recheck_range_v1(bit, 1);
    }
  };
  assert(pending[0] == UINT64_MAX && pending[1] == 1);
  assert(__obelisk_eval_route_promotion_boundary_v1(-1) == -1);
  assert(pending[0] == UINT64_MAX && pending[1] == 1);
  boundary();
  auto known0 = __obelisk_eval_selected_variant_v1_0;
  auto known63 = __obelisk_eval_selected_variant_v1_63;
  auto known64 = __obelisk_eval_selected_variant_v1_64;
  assert(known0 && known63 && known64);
  assert(!pending[0] && !pending[1]);
  assert(!__obelisk_eval_route_promotion_dirty_v1);

  // Touch only the second word. An unsuccessful boundary must leave its work
  // pending, and a failed proof is consumed without repeatedly rescanning it.
  change(72, true);
  assert(!pending[0] && pending[1] == 1);
  assert(__obelisk_eval_route_promotion_dirty_v1);
  assert(__obelisk_eval_selected_variant_v1_0 == known0);
  assert(__obelisk_eval_selected_variant_v1_63 == known63);
  assert(__obelisk_eval_selected_variant_v1_64 != known64);
  assert(__obelisk_eval_route_promotion_boundary_v1(-1) == -1);
  assert(!pending[0] && pending[1] == 1);
  boundary();
  assert(!pending[0] && !pending[1]);
  assert(!__obelisk_eval_route_promotion_dirty_v1);

  // Deliberately withhold recovery notification for bit 72. Processing word
  // zero must neither inspect nor promote the failed route in word one.
  __obelisk_state_unknown[9] = 0;
  change(71, true);
  assert(pending[0] == (uint64_t{1} << 63) && !pending[1]);
  boundary();
  assert(__obelisk_eval_selected_variant_v1_63 != known63);
  assert(__obelisk_eval_selected_variant_v1_64 != known64);
  change(72, false);
  assert(!pending[0] && pending[1] == 1);
  boundary();
  assert(__obelisk_eval_selected_variant_v1_64 == known64);
  assert(__obelisk_eval_selected_variant_v1_63 != known63);
  change(71, false);
  boundary();
  assert(__obelisk_eval_selected_variant_v1_63 == known63);

  // Shared input loss affects both words; unused packed padding affects none.
  change(0, true);
  assert(pending[0] == UINT64_MAX && pending[1] == 1);
  boundary();
  assert(__obelisk_eval_selected_variant_v1_0 != known0);
  assert(__obelisk_eval_selected_variant_v1_63 != known63);
  assert(__obelisk_eval_selected_variant_v1_64 != known64);
  change(0, false);
  boundary();
  change(1, true);
  assert(!pending[0] && !pending[1]);
  assert(!__obelisk_eval_route_promotion_dirty_v1);
  boundary();
  assert(__obelisk_eval_selected_variant_v1_0 == known0);
  assert(__obelisk_eval_selected_variant_v1_63 == known63);
  assert(__obelisk_eval_selected_variant_v1_64 == known64);
  assert(__obelisk_state_unknown[0] == 2);
  // Kernel certificates use independent latch and pending state. Exercise
  // every packed bit, both bitmap words, and the unused neighboring bits.
  // Kernel owners follow symbol order (writer0, writer1, writer10, ...).
  unsigned writers[65];
  for (unsigned i = 0; i != 65; ++i)
    writers[i] = i;
  std::sort(writers, writers + 65, [](unsigned a, unsigned b) {
    char lhs[3], rhs[3];
    std::snprintf(lhs, sizeof(lhs), "%u", a);
    std::snprintf(rhs, sizeof(rhs), "%u", b);
    return std::strcmp(lhs, rhs) < 0;
  });
  for (unsigned bit = 0; bit != 80; ++bit) {
    std::memset(__obelisk_state_unknown, 0, 10);
    __obelisk_eval_promotion_recheck_range_v1(0, 80);
    for (unsigned owner = 0; owner != 65; ++owner)
      assert(__obelisk_eval_kernel_promotion_ready_v1(owner));
    auto &kernelPending = __obelisk_eval_promotion_pending_mask_v1;
    assert(!kernelPending[0] && !kernelPending[1]);
    change(bit, true);
    uint64_t expected[2] = {};
    for (unsigned owner = 0; owner != 65; ++owner) {
      bool affected = bit == 0 || bit == writers[owner] + 8;
      assert(__obelisk_eval_kernel_promotion_ready_v1(owner) == !affected);
      assert(__obelisk_eval_kernel_promotion_latched_v1[owner] == !affected);
      if (affected)
        expected[owner / 64] |= uint64_t{1} << (owner % 64);
    }
    assert(kernelPending[0] == expected[0] && kernelPending[1] == expected[1]);
    assert(__obelisk_state_unknown[bit / 8] == (1u << (bit % 8)));
    change(bit, false);
    for (unsigned owner = 0; owner != 65; ++owner)
      assert(__obelisk_eval_kernel_promotion_ready_v1(owner));
    assert(!kernelPending[0] && !kernelPending[1]);
  }
  std::puts("promotion word scans passed");
}
