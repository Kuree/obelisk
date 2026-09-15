#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t __obelisk_state_unknown[];
extern void (*__obelisk_eval_function_route_v1_0)(void *);
extern void (*__obelisk_eval_function_route_v1_1)(void *);
extern uint8_t __obelisk_eval_kernel_promotion_latched_v1[];
extern uint64_t __obelisk_eval_promotion_pending_mask_v1[];
extern uint64_t __obelisk_eval_route_promotion_pending_v1[];
extern uint8_t __obelisk_eval_promotion_latched_v1;
extern uint8_t __obelisk_eval_step_four_state_fallback_v1;
extern uint64_t __obelisk_eval_fast_nba_roots_v1[];
int __obelisk_eval_four_state_nba_handoff_v1(void *, void *, void *);
void __obelisk_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
void executeFourStateWork(void *) asm("work.__obelisk_eval_body_0");
void __obelisk_eval_promotion_invalidate_mask_v1(uint64_t, uint64_t);
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
  constexpr unsigned bytes = (bits + 16) / 8;
  std::memset(__obelisk_state_unknown, 0, bytes);
  __obelisk_eval_promotion_recheck_range_v1(0, bytes * 8);
  __obelisk_eval_route_promotion_scan_v1();
  auto known = __obelisk_eval_function_route_v1_0;
  auto otherKnown = __obelisk_eval_function_route_v1_1;
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
    assert(__obelisk_eval_route_promotion_pending_v1[0] ==
           (unsigned(certified(bit)) | (unsigned(bit >= 376) << 1)));
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
    __obelisk_eval_promotion_invalidate_range_v1(bit, 1);
    check();
    __obelisk_state_unknown[bit / 8] = 0;
    __obelisk_eval_promotion_recheck_range_v1(bit, 1);
  }
  // All uncertified bits are unknown simultaneously, including both partial
  // boundary bytes. They must not prevent promotion or be cleared by a scan.
  for (unsigned bit = 0; bit != bits; ++bit)
    if (!certified(bit)) {
      __obelisk_state_unknown[bit / 8] |= 1u << (bit % 8);
      __obelisk_eval_promotion_invalidate_range_v1(bit, 1);
    }
  check();
  uint8_t original[bytes];
  std::memcpy(original, __obelisk_state_unknown, bytes);
  for (unsigned bit = 0; bit != bits; ++bit) {
    if (!certified(bit))
      continue;
    __obelisk_state_unknown[bit / 8] |= 1u << (bit % 8);
    __obelisk_eval_promotion_invalidate_range_v1(bit, 1);
    check();
    __obelisk_state_unknown[bit / 8] &= ~(1u << (bit % 8));
    __obelisk_eval_promotion_recheck_range_v1(bit, 1);
  }
  assert(std::memcmp(original, __obelisk_state_unknown, bytes) == 0);
  // Exercise actual generated blocking stores. The input mutation is reported
  // separately; only the subsequent output transition should dirty proofs.
  std::memset(__obelisk_state_unknown, 0, bytes);
  __obelisk_eval_promotion_recheck_range_v1(0, bytes * 8);
  auto promote = [&] {
    __obelisk_eval_route_promotion_scan_v1();
    assert(__obelisk_eval_kernel_promotion_ready_v1_0());
    assert(__obelisk_eval_kernel_promotion_ready_v1_1());
  };
  promote();
  executeFourStateWork(nullptr);
  assert((__obelisk_eval_route_promotion_pending_v1[0] != 0) == 0);
  assert(__obelisk_eval_promotion_pending_mask_v1[0] == 0);
  __obelisk_state_unknown[11 / 8] |= 1u << (11 % 8);
  __obelisk_eval_promotion_invalidate_range_v1(11, 1);
  __obelisk_eval_route_promotion_scan_v1();
  assert((__obelisk_eval_route_promotion_pending_v1[0] != 0) == 0);
  executeFourStateWork(nullptr);
  assert(__obelisk_state_unknown[168 / 8] & 1);
  assert((__obelisk_eval_route_promotion_pending_v1[0] != 0) == 1);
  assert(__obelisk_eval_function_route_v1_1 == otherKnown);
  assert(__obelisk_eval_kernel_promotion_latched_v1[0] == 1);
  __obelisk_eval_route_promotion_scan_v1();
  executeFourStateWork(nullptr);
  assert((__obelisk_eval_route_promotion_pending_v1[0] != 0) == 0);
  __obelisk_state_unknown[11 / 8] = 0;
  __obelisk_eval_promotion_invalidate_range_v1(11, 1);
  __obelisk_eval_route_promotion_scan_v1();
  executeFourStateWork(nullptr);
  assert(__obelisk_state_unknown[168 / 8] == 0);
  assert((__obelisk_eval_route_promotion_pending_v1[0] != 0) == 1);
  promote();
  assert(__obelisk_eval_function_route_v1_0 == known);

  // Recovery preserves positive aggregate certificates; only a subsequent
  // loss may clear them. Their inputs are known at this test boundary.
  __obelisk_eval_promotion_latched_v1 = 1;
  __obelisk_eval_promotion_recheck_range_v1(11, 1);
  assert(__obelisk_eval_promotion_latched_v1 == 1);
  assert(__obelisk_eval_function_route_v1_0 == known);
  assert(__obelisk_eval_function_route_v1_1 == otherKnown);

  // A sparse word must not expand to a bounding interval: bit 368 is a gap,
  // bit 376 belongs only to the second proof. Also exercise a complete run
  // of 64 bits, whose length must never become an undefined shift by 64.
  __obelisk_eval_promotion_invalidate_mask_v1(340, (uint64_t{1} << 28) |
                                                       (uint64_t{1} << 36));
  assert(__obelisk_eval_function_route_v1_0 == known);
  assert(__obelisk_eval_function_route_v1_1 != otherKnown);
  promote();
  __obelisk_eval_promotion_invalidate_mask_v1(304, UINT64_MAX);
  assert(__obelisk_eval_function_route_v1_0 != known);
  assert(__obelisk_eval_function_route_v1_1 == otherKnown);
  // NBA destination proofs are distinct, even if a source kernel's proof is
  // already false. Root ordering is private; exactly one of two bits must
  // survive each disjoint destination write, including a partial-byte write.
  __obelisk_eval_fast_nba_roots_v1[0] = 3;
  __obelisk_eval_promotion_invalidate_range_v1(409, 1);
  uint64_t remainingRoot = __obelisk_eval_fast_nba_roots_v1[0];
  assert(remainingRoot == 1 || remainingRoot == 2);
  __obelisk_eval_promotion_recheck_range_v1(409, 1);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == remainingRoot);
  __obelisk_eval_promotion_invalidate_range_v1(419, 1);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == 0);
  // Exercise the real NBA handoff, not just its certificate hooks. A known
  // payload is deferred until commit and must clear an unknown destination.
  // The unrelated group stays promoted throughout the exceptional barrier.
  std::memset(__obelisk_state_unknown, 0, bytes);
  __obelisk_eval_promotion_recheck_range_v1(0, bytes * 8);
  promote();
  __obelisk_eval_fast_nba_roots_v1[0] = 3;
  __obelisk_state_unknown[408 / 8] = 0x81;
  __obelisk_eval_promotion_invalidate_range_v1(408, 8);
  remainingRoot = __obelisk_eval_fast_nba_roots_v1[0];
  assert(remainingRoot == 1 || remainingRoot == 2);
  executeFourStateWork(nullptr);
  assert(__obelisk_state_unknown[408 / 8] == 0x81);
  assert(__obelisk_eval_function_route_v1_1 == otherKnown);
  assert(__obelisk_eval_kernel_promotion_latched_v1[0] == 1);
  assert(__obelisk_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__obelisk_state_unknown[408 / 8] == 0);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == remainingRoot);
  assert(__obelisk_eval_function_route_v1_1 == otherKnown);
  assert(__obelisk_eval_kernel_promotion_latched_v1[0] == 1);
  assert((__obelisk_eval_route_promotion_pending_v1[0] != 0) == 1);
  // Enter through the actual four-state route wrapper with known inputs.
  // Conservative route invalidation does not mean an NBA destination changed.
  // Record staged four-state provenance without discarding either root proof.
  promote();
  __obelisk_eval_promotion_invalidate_range_v1(11, 1);
  auto fallback = __obelisk_eval_function_route_v1_0;
  assert(fallback != known);
  __obelisk_eval_fast_nba_roots_v1[0] = 3;
  __obelisk_eval_step_four_state_fallback_v1 = 0;
  fallback(nullptr);
  assert(__obelisk_eval_step_four_state_fallback_v1 == 1);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == 3);
  assert(__obelisk_eval_function_route_v1_1 == otherKnown);
  // A failed proof is consumed once. Changing its bytes without reporting a
  // mutation must not silently rescan it when an unrelated route is queued.
  // Reporting recovery then queues exactly that failed route, not its peer.
  __obelisk_eval_route_promotion_scan_v1();
  __obelisk_state_unknown[11 / 8] |= 1u << (11 % 8);
  __obelisk_eval_promotion_invalidate_range_v1(11, 1);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 1);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_function_route_v1_0 != known);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 0);
  __obelisk_state_unknown[11 / 8] = 0;
  __obelisk_eval_promotion_invalidate_range_v1(376, 1);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 2);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_function_route_v1_0 != known);
  assert(__obelisk_eval_function_route_v1_1 == otherKnown);
  __obelisk_eval_promotion_recheck_range_v1(11, 1);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 1);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_function_route_v1_0 == known);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 0);
  __obelisk_eval_promotion_recheck_range_v1(376, 1);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 0);
  std::puts("promotion word scans passed");
}
