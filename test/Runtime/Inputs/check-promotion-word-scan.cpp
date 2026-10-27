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
extern uint8_t __talos_state_unknown[];
extern uint8_t __talos_eval_kernel_promotion_latched_v1[];
extern uint64_t __talos_eval_promotion_pending_mask_v1[];
extern uint8_t __talos_eval_promotion_latched_v1;
extern uint8_t __talos_eval_step_four_state_fallback_v1;
extern uint64_t __talos_eval_fast_nba_roots_v1[];
extern uint64_t __talos_eval_step_four_state_nba_roots_v1[];
int __talos_eval_four_state_nba_handoff_v1(void *, void *, void *);
void __talos_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
int executeSelectedWork(void *) asm(
    TALOS_TEST_SYMBOL("__talos_direct_fragment_2_12.__talos_execute"));
int executeTrustedWork(void *) asm(
    TALOS_TEST_SYMBOL("__talos_direct_fragment_2_12.__talos_"
                        "execute.two_state.__talos_trusted"));
void executeFourStateWork(void *) asm(
    TALOS_TEST_SYMBOL("work.__talos_eval_body_0"));
void executeOtherFourStateWork(void *) asm(
    TALOS_TEST_SYMBOL("other_work.__talos_eval_body_0"));
void __talos_eval_promotion_invalidate_mask_v1(uint64_t, uint64_t);
bool __talos_eval_kernel_promotion_ready_v1(uint64_t);
void __talos_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
}

static void refreshProofs() {
  (void)__talos_eval_kernel_promotion_ready_v1(0);
  (void)__talos_eval_kernel_promotion_ready_v1(1);
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
  std::memset(__talos_state_unknown, 0, bytes);
  __talos_eval_promotion_recheck_range_v1(0, bytes * 8);
  refreshProofs();
  auto known = __talos_eval_kernel_promotion_latched_v1[1];
  auto otherKnown = __talos_eval_kernel_promotion_latched_v1[0];
  assert(known && otherKnown);
  // Exercise the emitted reverse index through its real runtime helper.
  // Every certified bit invalidates this route, while holes and partial-byte
  // neighbors preserve it. Invalidation must never alter canonical X/Z state.
  for (unsigned bit = 0; bit != bits; ++bit) {
    refreshProofs();
    assert(__talos_eval_kernel_promotion_latched_v1[1] == known);
    assert(__talos_eval_kernel_promotion_latched_v1[0] == otherKnown);
    assert(__talos_eval_kernel_promotion_ready_v1(0));
    assert(__talos_eval_kernel_promotion_ready_v1(1));
    assert(__talos_eval_promotion_pending_mask_v1[0] == 0);
    __talos_eval_promotion_invalidate_range_v1(bit, 1);
    assert((__talos_eval_kernel_promotion_latched_v1[1] != known) ==
           certified(bit));
    bool otherAffected = bit >= 400;
    assert((__talos_eval_kernel_promotion_latched_v1[0] != otherKnown) ==
           otherAffected);
    assert(__talos_eval_kernel_promotion_latched_v1[0] == !otherAffected);
    assert(__talos_eval_kernel_promotion_latched_v1[1] == !certified(bit));
    assert(__talos_eval_promotion_pending_mask_v1[0] ==
           (unsigned(otherAffected) | (unsigned(certified(bit)) << 1)));
    for (unsigned byte = 0; byte != bytes; ++byte)
      assert(__talos_state_unknown[byte] == 0);
  }
  refreshProofs();
  auto check = [&] {
    bool expectedKnown = true;
    for (unsigned bit = 0; bit != bits; ++bit)
      if (certified(bit) &&
          (__talos_state_unknown[bit / 8] & (1u << (bit % 8))))
        expectedKnown = false;
    refreshProofs();
    assert((__talos_eval_kernel_promotion_latched_v1[1] == known) ==
           expectedKnown);
    bool otherExpectedKnown = true;
    for (unsigned bit = 400; bit != bits; ++bit)
      if (__talos_state_unknown[bit / 8] & (1u << (bit % 8)))
        otherExpectedKnown = false;
    assert(__talos_eval_kernel_promotion_ready_v1(0) == otherExpectedKnown);
    assert(__talos_eval_kernel_promotion_ready_v1(1) == expectedKnown);
    assert(__talos_eval_kernel_promotion_latched_v1[0] == otherExpectedKnown);
    assert(__talos_eval_kernel_promotion_latched_v1[1] == expectedKnown);
    assert(__talos_eval_promotion_pending_mask_v1[0] ==
           (unsigned(!otherExpectedKnown) | (unsigned(!expectedKnown) << 1)));
  };
  // Every bit individually: boundaries, overlapping views, full words, and
  // holes must agree with the scalar union-of-bit-ranges reference.
  for (unsigned bit = 0; bit != bits; ++bit) {
    __talos_state_unknown[bit / 8] |= 1u << (bit % 8);
    __talos_eval_promotion_invalidate_range_v1(bit, 1);
    check();
    __talos_state_unknown[bit / 8] = 0;
    __talos_eval_promotion_recheck_range_v1(bit, 1);
  }
  // A negative hint must be rechecked, and clearing it must resume a complete
  // proof. Move the remaining X between distant words, including back before
  // the previous hint, while unrelated owners continue to promote.
  for (unsigned bit : {13u, 326u}) {
    __talos_state_unknown[bit / 8] |= 1u << (bit % 8);
    __talos_eval_promotion_invalidate_range_v1(bit, 1);
  }
  for (unsigned retry = 0; retry != 8; ++retry)
    check();
  __talos_state_unknown[13 / 8] = 0;
  __talos_eval_promotion_recheck_range_v1(13, 1);
  check();
  __talos_state_unknown[13 / 8] = 1u << (13 % 8);
  __talos_eval_promotion_invalidate_range_v1(13, 1);
  __talos_state_unknown[326 / 8] = 0;
  __talos_eval_promotion_recheck_range_v1(326, 1);
  check();
  __talos_state_unknown[13 / 8] = 0;
  __talos_eval_promotion_recheck_range_v1(13, 1);
  check();

  // The state plane includes guard bytes beyond the last root. Unknown
  // padding is outside the proof. A write-only NBA destination (447) has a
  // separate commit certificate, not an activation input (LRM 4.9.4, 10.4.2).
  // The final live input bit (431) must still defeat activation promotion,
  // including repeated queries and recovery from a hint.
  for (unsigned byte = 56; byte != 64; ++byte)
    __talos_state_unknown[byte] = 0xff;
  __talos_eval_promotion_invalidate_range_v1(448, 8);
  assert(__talos_eval_kernel_promotion_ready_v1(0));
  __talos_eval_fast_nba_roots_v1[0] = 3;
  __talos_state_unknown[55] = 0x80;
  __talos_eval_promotion_invalidate_range_v1(447, 1);
  assert(__talos_eval_kernel_promotion_ready_v1(0));
  assert(__talos_eval_fast_nba_roots_v1[0] == 1 ||
         __talos_eval_fast_nba_roots_v1[0] == 2);
  __talos_state_unknown[53] = 0x80;
  __talos_eval_promotion_invalidate_range_v1(431, 1);
  for (unsigned retry = 0; retry != 8; ++retry)
    assert(!__talos_eval_kernel_promotion_ready_v1(0));
  assert(__talos_eval_kernel_promotion_ready_v1(1));
  __talos_state_unknown[53] = 0;
  __talos_eval_promotion_recheck_range_v1(431, 1);
  assert(__talos_eval_kernel_promotion_ready_v1(0));
  __talos_state_unknown[55] = 0;
  __talos_eval_promotion_recheck_range_v1(447, 1);
  assert(__talos_eval_kernel_promotion_ready_v1(0));
  for (unsigned byte = 56; byte != 64; ++byte) {
    assert(__talos_state_unknown[byte] == 0xff);
    __talos_state_unknown[byte] = 0;
  }
  // All uncertified bits are unknown simultaneously, including both partial
  // boundary bytes. They must not prevent promotion or be cleared by a scan.
  for (unsigned bit = 0; bit != bits; ++bit)
    if (!certified(bit)) {
      __talos_state_unknown[bit / 8] |= 1u << (bit % 8);
      __talos_eval_promotion_invalidate_range_v1(bit, 1);
    }
  check();
  uint8_t original[bytes];
  std::memcpy(original, __talos_state_unknown, bytes);
  for (unsigned bit = 0; bit != bits; ++bit) {
    if (!certified(bit))
      continue;
    __talos_state_unknown[bit / 8] |= 1u << (bit % 8);
    __talos_eval_promotion_invalidate_range_v1(bit, 1);
    check();
    __talos_state_unknown[bit / 8] &= ~(1u << (bit % 8));
    __talos_eval_promotion_recheck_range_v1(bit, 1);
  }
  assert(std::memcmp(original, __talos_state_unknown, bytes) == 0);
  // Exercise actual generated blocking stores. The input mutation is reported
  // separately; only the subsequent output transition should dirty proofs.
  std::memset(__talos_state_unknown, 0, bytes);
  __talos_eval_promotion_recheck_range_v1(0, bytes * 8);
  auto promote = [&] {
    refreshProofs();
    assert(__talos_eval_kernel_promotion_ready_v1(0));
    assert(__talos_eval_kernel_promotion_ready_v1(1));
  };
  promote();
  executeFourStateWork(nullptr);
  assert(__talos_eval_promotion_pending_mask_v1[0] == 0);
  __talos_state_unknown[11 / 8] |= 1u << (11 % 8);
  __talos_eval_promotion_invalidate_range_v1(11, 1);
  refreshProofs();
  executeFourStateWork(nullptr);
  assert(__talos_state_unknown[168 / 8] & 1);
  assert(__talos_eval_kernel_promotion_latched_v1[0] == otherKnown);
  assert(__talos_eval_kernel_promotion_latched_v1[0] == 1);
  refreshProofs();
  executeFourStateWork(nullptr);
  __talos_state_unknown[11 / 8] = 0;
  __talos_eval_promotion_invalidate_range_v1(11, 1);
  refreshProofs();
  executeFourStateWork(nullptr);
  assert(__talos_state_unknown[168 / 8] == 0);
  promote();
  assert(__talos_eval_kernel_promotion_latched_v1[1] == known);

  // Recovery preserves positive aggregate certificates; only a subsequent
  // loss may clear them. Their inputs are known at this test boundary.
  __talos_eval_promotion_latched_v1 = 1;
  __talos_eval_promotion_recheck_range_v1(11, 1);
  assert(__talos_eval_promotion_latched_v1 == 1);
  assert(__talos_eval_kernel_promotion_latched_v1[1] == known);
  assert(__talos_eval_kernel_promotion_latched_v1[0] == otherKnown);

  // A sparse word must not expand to a bounding interval: bit 384 is a gap,
  // bit 400 belongs only to the second proof. Also exercise a complete run
  // of 64 bits, whose length must never become an undefined shift by 64.
  __talos_eval_promotion_invalidate_mask_v1(356, (uint64_t{1} << 28) |
                                                       (uint64_t{1} << 44));
  assert(__talos_eval_kernel_promotion_latched_v1[1] == known);
  assert(__talos_eval_kernel_promotion_latched_v1[0] != otherKnown);
  promote();
  __talos_eval_promotion_invalidate_mask_v1(320, UINT64_MAX);
  assert(__talos_eval_kernel_promotion_latched_v1[1] != known);
  assert(__talos_eval_kernel_promotion_latched_v1[0] == otherKnown);
  // NBA destination proofs are distinct, even if a source kernel's proof is
  // already false. Root ordering is private; exactly one of two bits must
  // survive each disjoint destination write, including a partial-byte write.
  __talos_eval_fast_nba_roots_v1[0] = 3;
  __talos_eval_promotion_invalidate_range_v1(433, 1);
  uint64_t remainingRoot = __talos_eval_fast_nba_roots_v1[0];
  assert(remainingRoot == 1 || remainingRoot == 2);
  __talos_eval_promotion_recheck_range_v1(433, 1);
  assert(__talos_eval_fast_nba_roots_v1[0] == remainingRoot);
  __talos_eval_promotion_invalidate_range_v1(443, 1);
  assert(__talos_eval_fast_nba_roots_v1[0] == 0);
  // Exercise the real NBA handoff, not just its certificate hooks. A known
  // payload is deferred until commit and must clear an unknown destination.
  // The unrelated group stays promoted throughout the exceptional barrier.
  std::memset(__talos_state_unknown, 0, bytes);
  __talos_eval_promotion_recheck_range_v1(0, bytes * 8);
  promote();
  __talos_eval_fast_nba_roots_v1[0] = 3;
  __talos_state_unknown[432 / 8] = 0x81;
  __talos_eval_promotion_invalidate_range_v1(432, 8);
  remainingRoot = __talos_eval_fast_nba_roots_v1[0];
  assert(remainingRoot == 1 || remainingRoot == 2);
  executeFourStateWork(nullptr);
  assert(__talos_state_unknown[432 / 8] == 0x81);
  assert(__talos_eval_kernel_promotion_latched_v1[0] == otherKnown);
  assert(__talos_eval_kernel_promotion_latched_v1[0] == 1);
  assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__talos_state_unknown[432 / 8] == 0);
  assert(__talos_eval_fast_nba_roots_v1[0] == remainingRoot);
  assert(__talos_eval_kernel_promotion_latched_v1[0] == otherKnown);
  assert(__talos_eval_kernel_promotion_latched_v1[0] == 1);
  // Only the NBA destination changed; neither activation reads it.
  // A route uses the shared proof on demand. Invalidating a known input
  // must refresh the certificate without executing the scheduled owner.
  promote();
  __talos_eval_promotion_invalidate_range_v1(11, 1);
  assert(__talos_eval_kernel_promotion_latched_v1[1] != known);
  __talos_eval_fast_nba_roots_v1[0] = 3;
  __talos_eval_step_four_state_fallback_v1 = 0;
  assert(executeSelectedWork(nullptr) == 0);
  assert(__talos_eval_step_four_state_fallback_v1 == 0);
  assert(__talos_eval_kernel_promotion_latched_v1[1] == known);
  // An actual X must still choose the four-state body. Its constant NBA
  // payload remains known even though the activation input is not.
  __talos_state_unknown[11 / 8] |= 1u << (11 % 8);
  __talos_eval_promotion_invalidate_range_v1(11, 1);
  assert(executeSelectedWork(nullptr) == 0);
  assert(__talos_eval_step_four_state_fallback_v1 == 1);
  assert(__talos_eval_fast_nba_roots_v1[0] == 3);
  assert(__talos_eval_kernel_promotion_latched_v1[0] == otherKnown);
  // IEEE 1800-2023 6.3.1: four-state payloads can be known. The same proof
  // survives the barrier even when every root has four-state provenance.
  __talos_eval_step_four_state_nba_roots_v1[0] = 3;
  assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__talos_eval_fast_nba_roots_v1[0] == 3);
  __talos_state_unknown[11 / 8] = 0;
  __talos_eval_promotion_recheck_range_v1(11, 1);
  executeFourStateWork(nullptr);
  assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  // IEEE 1800-2023 4.9.4, 10.4.2: staging X preserves the destination until
  // the NBA region. The commit revokes its proof and preserves the other root.
  __talos_state_unknown[400 / 8] = 0x81;
  __talos_eval_promotion_invalidate_range_v1(400, 8);
  executeOtherFourStateWork(nullptr);
  assert(__talos_state_unknown[440 / 8] == 0);
  assert(__talos_eval_fast_nba_roots_v1[0] == 3);
  assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__talos_state_unknown[440 / 8] == 0x81);
  uint64_t preservedRoot = __talos_eval_fast_nba_roots_v1[0];
  assert(preservedRoot == 1 || preservedRoot == 2);
  __talos_state_unknown[400 / 8] = 0;
  __talos_eval_promotion_recheck_range_v1(400, 8);
  executeOtherFourStateWork(nullptr);
  assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__talos_state_unknown[440 / 8] == 0);
  assert(__talos_eval_fast_nba_roots_v1[0] == preservedRoot);
  __talos_eval_fast_nba_roots_v1[0] = 3;
  // The same direct selector enters the promoted branch after recovery,
  // without recording four-state provenance or resetting destination proofs.
  promote();
  __talos_eval_step_four_state_fallback_v1 = 0;
  assert(executeSelectedWork(nullptr) == 0);
  assert(__talos_eval_step_four_state_fallback_v1 == 0);
  assert(__talos_eval_fast_nba_roots_v1[0] == 3);
  // IEEE 1800-2023 6.3.1: the trusted entry consumes the established proof.
  promote();
  assert(executeTrustedWork(nullptr) == 0);
  assert(__talos_eval_step_four_state_fallback_v1 == 0);
  assert(__talos_eval_fast_nba_roots_v1[0] == 3);
  // Querying an unrelated owner must not refresh this failed proof. The
  // exact owner query rechecks its negative hint and sees recovery.
  refreshProofs();
  __talos_state_unknown[11 / 8] |= 1u << (11 % 8);
  __talos_eval_promotion_invalidate_range_v1(11, 1);
  assert(!__talos_eval_kernel_promotion_ready_v1(1));
  assert(__talos_eval_promotion_pending_mask_v1[0] == 2);
  __talos_state_unknown[11 / 8] = 0;
  __talos_eval_promotion_invalidate_range_v1(400, 1);
  assert(__talos_eval_kernel_promotion_ready_v1(0));
  assert(__talos_eval_kernel_promotion_latched_v1[1] != known);
  assert(__talos_eval_promotion_pending_mask_v1[0] == 2);
  __talos_eval_promotion_recheck_range_v1(11, 1);
  assert(__talos_eval_kernel_promotion_ready_v1(1));
  assert(__talos_eval_promotion_pending_mask_v1[0] == 0);
  std::puts("promotion word scans passed");
}
