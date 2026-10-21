#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

// asm labels are final symbol names; Mach-O prefixes C symbols with '_'.
#define OBELISK_TEST_STRINGIFY_(text) #text
#define OBELISK_TEST_STRINGIFY(text) OBELISK_TEST_STRINGIFY_(text)
#define OBELISK_TEST_SYMBOL(name)                                              \
  OBELISK_TEST_STRINGIFY(__USER_LABEL_PREFIX__) name

extern "C" {
extern uint8_t __obelisk_state_unknown[];
extern uint8_t __obelisk_eval_selected_variant_v1_0;
extern uint8_t __obelisk_eval_selected_variant_v1_1;
extern uint8_t __obelisk_eval_kernel_promotion_latched_v1[];
extern uint64_t __obelisk_eval_promotion_pending_mask_v1[];
extern uint64_t __obelisk_eval_route_promotion_pending_v1[];
extern uint8_t __obelisk_eval_promotion_latched_v1;
extern uint8_t __obelisk_eval_step_four_state_fallback_v1;
extern uint64_t __obelisk_eval_fast_nba_roots_v1[];
int __obelisk_eval_four_state_nba_handoff_v1(void *, void *, void *);
void __obelisk_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
int executeSelectedWork(void *) asm(
    OBELISK_TEST_SYMBOL("__obelisk_direct_fragment_2_12.__obelisk_execute"));
int executeTrustedWork(void *) asm(
    OBELISK_TEST_SYMBOL("__obelisk_direct_fragment_2_12.__obelisk_"
                        "execute.two_state.__obelisk_trusted"));
void executeFourStateWork(void *) asm(
    OBELISK_TEST_SYMBOL("work.__obelisk_eval_body_0"));
void __obelisk_eval_promotion_invalidate_mask_v1(uint64_t, uint64_t);
bool __obelisk_eval_kernel_promotion_ready_v1(uint64_t);
void __obelisk_eval_route_promotion_scan_v1();
void __obelisk_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
}

static bool certified(unsigned bit) {
  return (bit >= 11 && bit < 140) || (bit >= 168 && bit < 297) ||
         (bit >= 320 && bit < 384);
}

int main() {
  // Root offsets: clock 0, input 8, output 168, other 320, clock 384,
  // other input 400, other output 416, NBA destinations 432 and 440.
  constexpr unsigned bits = 432;
  constexpr unsigned bytes = (bits + 16) / 8;
  std::memset(__obelisk_state_unknown, 0, bytes);
  __obelisk_eval_promotion_recheck_range_v1(0, bytes * 8);
  __obelisk_eval_route_promotion_scan_v1();
  auto known = __obelisk_eval_selected_variant_v1_0;
  auto otherKnown = __obelisk_eval_selected_variant_v1_1;
  assert(known && otherKnown);
  // Exercise the emitted reverse index through its real runtime helper.
  // Every certified bit invalidates this route, while holes and partial-byte
  // neighbors preserve it. Invalidation must never alter canonical X/Z state.
  for (unsigned bit = 0; bit != bits; ++bit) {
    __obelisk_eval_route_promotion_scan_v1();
    assert(__obelisk_eval_selected_variant_v1_0 == known);
    assert(__obelisk_eval_selected_variant_v1_1 == otherKnown);
    assert(__obelisk_eval_kernel_promotion_ready_v1(0));
    assert(__obelisk_eval_kernel_promotion_ready_v1(1));
    assert(__obelisk_eval_promotion_pending_mask_v1[0] == 0);
    __obelisk_eval_promotion_invalidate_range_v1(bit, 1);
    assert(__obelisk_eval_route_promotion_pending_v1[0] ==
           (unsigned(certified(bit)) | (unsigned(bit >= 400) << 1)));
    assert((__obelisk_eval_selected_variant_v1_0 != known) == certified(bit));
    bool otherAffected = bit >= 400;
    assert((__obelisk_eval_selected_variant_v1_1 != otherKnown) ==
           otherAffected);
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
    assert((__obelisk_eval_selected_variant_v1_0 == known) == expectedKnown);
    bool otherExpectedKnown = true;
    for (unsigned bit = 400; bit != bits; ++bit)
      if (__obelisk_state_unknown[bit / 8] & (1u << (bit % 8)))
        otherExpectedKnown = false;
    assert(__obelisk_eval_kernel_promotion_ready_v1(0) == otherExpectedKnown);
    assert(__obelisk_eval_kernel_promotion_ready_v1(1) == expectedKnown);
    assert(__obelisk_eval_kernel_promotion_latched_v1[0] == otherExpectedKnown);
    assert(__obelisk_eval_kernel_promotion_latched_v1[1] == expectedKnown);
    assert(__obelisk_eval_promotion_pending_mask_v1[0] ==
           (unsigned(!otherExpectedKnown) | (unsigned(!expectedKnown) << 1)));
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
  // A negative hint must be rechecked, and clearing it must resume a complete
  // proof. Move the remaining X between distant words, including back before
  // the previous hint, while unrelated owners continue to promote.
  for (unsigned bit : {13u, 326u}) {
    __obelisk_state_unknown[bit / 8] |= 1u << (bit % 8);
    __obelisk_eval_promotion_invalidate_range_v1(bit, 1);
  }
  for (unsigned retry = 0; retry != 8; ++retry)
    check();
  __obelisk_state_unknown[13 / 8] = 0;
  __obelisk_eval_promotion_recheck_range_v1(13, 1);
  check();
  __obelisk_state_unknown[13 / 8] = 1u << (13 % 8);
  __obelisk_eval_promotion_invalidate_range_v1(13, 1);
  __obelisk_state_unknown[326 / 8] = 0;
  __obelisk_eval_promotion_recheck_range_v1(326, 1);
  check();
  __obelisk_state_unknown[13 / 8] = 0;
  __obelisk_eval_promotion_recheck_range_v1(13, 1);
  check();

  // The state plane includes guard bytes beyond the last root. Unknown
  // padding is outside the proof, but its final live bit (447) must still
  // defeat promotion, including repeated queries and recovery from a hint.
  for (unsigned byte = 56; byte != 64; ++byte)
    __obelisk_state_unknown[byte] = 0xff;
  __obelisk_eval_promotion_invalidate_range_v1(448, 8);
  assert(__obelisk_eval_kernel_promotion_ready_v1(0));
  __obelisk_state_unknown[55] = 0x80;
  __obelisk_eval_promotion_invalidate_range_v1(447, 1);
  for (unsigned retry = 0; retry != 8; ++retry)
    assert(!__obelisk_eval_kernel_promotion_ready_v1(0));
  assert(__obelisk_eval_kernel_promotion_ready_v1(1));
  __obelisk_state_unknown[55] = 0;
  __obelisk_eval_promotion_recheck_range_v1(447, 1);
  assert(__obelisk_eval_kernel_promotion_ready_v1(0));
  for (unsigned byte = 56; byte != 64; ++byte) {
    assert(__obelisk_state_unknown[byte] == 0xff);
    __obelisk_state_unknown[byte] = 0;
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
    assert(__obelisk_eval_kernel_promotion_ready_v1(0));
    assert(__obelisk_eval_kernel_promotion_ready_v1(1));
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
  assert(__obelisk_eval_selected_variant_v1_1 == otherKnown);
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
  assert(__obelisk_eval_selected_variant_v1_0 == known);

  // Recovery preserves positive aggregate certificates; only a subsequent
  // loss may clear them. Their inputs are known at this test boundary.
  __obelisk_eval_promotion_latched_v1 = 1;
  __obelisk_eval_promotion_recheck_range_v1(11, 1);
  assert(__obelisk_eval_promotion_latched_v1 == 1);
  assert(__obelisk_eval_selected_variant_v1_0 == known);
  assert(__obelisk_eval_selected_variant_v1_1 == otherKnown);

  // A sparse word must not expand to a bounding interval: bit 384 is a gap,
  // bit 400 belongs only to the second proof. Also exercise a complete run
  // of 64 bits, whose length must never become an undefined shift by 64.
  __obelisk_eval_promotion_invalidate_mask_v1(356, (uint64_t{1} << 28) |
                                                       (uint64_t{1} << 44));
  assert(__obelisk_eval_selected_variant_v1_0 == known);
  assert(__obelisk_eval_selected_variant_v1_1 != otherKnown);
  promote();
  __obelisk_eval_promotion_invalidate_mask_v1(320, UINT64_MAX);
  assert(__obelisk_eval_selected_variant_v1_0 != known);
  assert(__obelisk_eval_selected_variant_v1_1 == otherKnown);
  // NBA destination proofs are distinct, even if a source kernel's proof is
  // already false. Root ordering is private; exactly one of two bits must
  // survive each disjoint destination write, including a partial-byte write.
  __obelisk_eval_fast_nba_roots_v1[0] = 3;
  __obelisk_eval_promotion_invalidate_range_v1(433, 1);
  uint64_t remainingRoot = __obelisk_eval_fast_nba_roots_v1[0];
  assert(remainingRoot == 1 || remainingRoot == 2);
  __obelisk_eval_promotion_recheck_range_v1(433, 1);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == remainingRoot);
  __obelisk_eval_promotion_invalidate_range_v1(443, 1);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == 0);
  // Exercise the real NBA handoff, not just its certificate hooks. A known
  // payload is deferred until commit and must clear an unknown destination.
  // The unrelated group stays promoted throughout the exceptional barrier.
  std::memset(__obelisk_state_unknown, 0, bytes);
  __obelisk_eval_promotion_recheck_range_v1(0, bytes * 8);
  promote();
  __obelisk_eval_fast_nba_roots_v1[0] = 3;
  __obelisk_state_unknown[432 / 8] = 0x81;
  __obelisk_eval_promotion_invalidate_range_v1(432, 8);
  remainingRoot = __obelisk_eval_fast_nba_roots_v1[0];
  assert(remainingRoot == 1 || remainingRoot == 2);
  executeFourStateWork(nullptr);
  assert(__obelisk_state_unknown[432 / 8] == 0x81);
  assert(__obelisk_eval_selected_variant_v1_1 == otherKnown);
  assert(__obelisk_eval_kernel_promotion_latched_v1[0] == 1);
  assert(__obelisk_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__obelisk_state_unknown[432 / 8] == 0);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == remainingRoot);
  assert(__obelisk_eval_selected_variant_v1_1 == otherKnown);
  assert(__obelisk_eval_kernel_promotion_latched_v1[0] == 1);
  assert((__obelisk_eval_route_promotion_pending_v1[0] != 0) == 1);
  // Enter through the actual four-state route wrapper with known inputs.
  // Conservative route invalidation does not mean an NBA destination changed.
  // Record staged four-state provenance without discarding either root proof.
  promote();
  __obelisk_eval_promotion_invalidate_range_v1(11, 1);
  auto fallback = __obelisk_eval_selected_variant_v1_0;
  assert(fallback != known);
  __obelisk_eval_fast_nba_roots_v1[0] = 3;
  __obelisk_eval_step_four_state_fallback_v1 = 0;
  assert(executeSelectedWork(nullptr) == 0);
  assert(__obelisk_eval_step_four_state_fallback_v1 == 1);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == 3);
  assert(__obelisk_eval_selected_variant_v1_1 == otherKnown);
  // The same direct selector enters the promoted branch after recovery,
  // without recording four-state provenance or resetting destination proofs.
  promote();
  __obelisk_eval_step_four_state_fallback_v1 = 0;
  assert(executeSelectedWork(nullptr) == 0);
  assert(__obelisk_eval_step_four_state_fallback_v1 == 0);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == 3);
  // IEEE 1800-2023 6.3.1: the trusted entry consumes the established proof.
  promote();
  assert(executeTrustedWork(nullptr) == 0);
  assert(__obelisk_eval_step_four_state_fallback_v1 == 0);
  assert(__obelisk_eval_fast_nba_roots_v1[0] == 3);
  // A failed proof is consumed once. Changing its bytes without reporting a
  // mutation must not silently rescan it when an unrelated route is queued.
  // Reporting recovery then queues exactly that failed route, not its peer.
  __obelisk_eval_route_promotion_scan_v1();
  __obelisk_state_unknown[11 / 8] |= 1u << (11 % 8);
  __obelisk_eval_promotion_invalidate_range_v1(11, 1);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 1);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_selected_variant_v1_0 != known);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 0);
  __obelisk_state_unknown[11 / 8] = 0;
  __obelisk_eval_promotion_invalidate_range_v1(400, 1);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 2);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_selected_variant_v1_0 != known);
  assert(__obelisk_eval_selected_variant_v1_1 == otherKnown);
  __obelisk_eval_promotion_recheck_range_v1(11, 1);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 1);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_selected_variant_v1_0 == known);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 0);
  __obelisk_eval_promotion_recheck_range_v1(400, 1);
  assert(__obelisk_eval_route_promotion_pending_v1[0] == 0);
  std::puts("promotion word scans passed");
}
