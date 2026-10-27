#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t __talos_state_unknown[];
extern uint8_t __talos_eval_kernel_promotion_latched_v1[];
extern uint64_t __talos_eval_promotion_pending_mask_v1[];
bool __talos_eval_kernel_promotion_ready_v1(uint64_t);
void __talos_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
void __talos_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
}

int main() {
  // Routes share the owner cache. Owners follow symbol order
  // (writer0, writer1, writer10, ...), independently of source order.
  unsigned writers[65];
  for (unsigned i = 0; i != 65; ++i)
    writers[i] = i;
  std::sort(writers, writers + 65, [](unsigned a, unsigned b) {
    char lhs[3], rhs[3];
    std::snprintf(lhs, sizeof(lhs), "%u", a);
    std::snprintf(rhs, sizeof(rhs), "%u", b);
    return std::strcmp(lhs, rhs) < 0;
  });
  auto &pending = __talos_eval_promotion_pending_mask_v1;
  auto &latches = __talos_eval_kernel_promotion_latched_v1;
  auto change = [](unsigned bit, bool unknown) {
    uint8_t mask = 1u << (bit % 8);
    if (unknown) {
      __talos_state_unknown[bit / 8] |= mask;
      __talos_eval_promotion_invalidate_range_v1(bit, 1);
    } else {
      __talos_state_unknown[bit / 8] &= ~mask;
      __talos_eval_promotion_recheck_range_v1(bit, 1);
    }
  };
  std::memset(__talos_state_unknown, 0, 10);
  assert(pending[0] == UINT64_MAX && pending[1] == 1);
  for (unsigned owner = 0; owner != 65; ++owner)
    assert(__talos_eval_kernel_promotion_ready_v1(owner));
  assert(!pending[0] && !pending[1]);

  // A failed proof in word one must remain independent of word zero.
  unsigned bit64 = writers[64] + 8, bit63 = writers[63] + 8;
  change(bit64, true);
  assert(!pending[0] && pending[1] == 1);
  assert(latches[0] && latches[63] && !latches[64]);
  assert(!__talos_eval_kernel_promotion_ready_v1(64));
  __talos_state_unknown[bit64 / 8] &= ~(1u << (bit64 % 8));
  change(bit63, true);
  assert(!__talos_eval_kernel_promotion_ready_v1(63));
  assert(!latches[64]);
  assert(pending[0] == (uint64_t{1} << 63) && pending[1] == 1);
  change(bit64, false);
  assert(__talos_eval_kernel_promotion_ready_v1(64));
  assert(!latches[63] && !pending[1]);
  change(bit63, false);
  assert(__talos_eval_kernel_promotion_ready_v1(63));

  // A shared input invalidates both words; padding belongs to neither.
  change(0, true);
  assert(pending[0] == UINT64_MAX && pending[1] == 1);
  for (unsigned owner = 0; owner != 65; ++owner)
    assert(!__talos_eval_kernel_promotion_ready_v1(owner));
  change(0, false);
  for (unsigned owner = 0; owner != 65; ++owner)
    assert(__talos_eval_kernel_promotion_ready_v1(owner));
  change(1, true);
  assert(!pending[0] && !pending[1]);
  assert(latches[0] && latches[63] && latches[64]);
  assert(__talos_state_unknown[0] == 2);

  // Exhaust every packed bit, both bitmap words and unused neighbors.
  for (unsigned bit = 0; bit != 80; ++bit) {
    std::memset(__talos_state_unknown, 0, 10);
    __talos_eval_promotion_recheck_range_v1(0, 80);
    for (unsigned owner = 0; owner != 65; ++owner)
      assert(__talos_eval_kernel_promotion_ready_v1(owner));
    auto &kernelPending = __talos_eval_promotion_pending_mask_v1;
    assert(!kernelPending[0] && !kernelPending[1]);
    change(bit, true);
    uint64_t expected[2] = {};
    for (unsigned owner = 0; owner != 65; ++owner) {
      bool affected = bit == 0 || bit == writers[owner] + 8;
      assert(__talos_eval_kernel_promotion_ready_v1(owner) == !affected);
      assert(__talos_eval_kernel_promotion_latched_v1[owner] == !affected);
      if (affected)
        expected[owner / 64] |= uint64_t{1} << (owner % 64);
    }
    assert(kernelPending[0] == expected[0] && kernelPending[1] == expected[1]);
    assert(__talos_state_unknown[bit / 8] == (1u << (bit % 8)));
    change(bit, false);
    for (unsigned owner = 0; owner != 65; ++owner)
      assert(__talos_eval_kernel_promotion_ready_v1(owner));
    assert(!kernelPending[0] && !kernelPending[1]);
  }
  std::puts("promotion word scans passed");
}
