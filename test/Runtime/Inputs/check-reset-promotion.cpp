#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

// asm labels are final symbol names; Mach-O prefixes C symbols with '_'.
#define TALOS_TEST_STRINGIFY_(text) #text
#define TALOS_TEST_STRINGIFY(text) TALOS_TEST_STRINGIFY_(text)
#define TALOS_TEST_SYMBOL(name)                                              \
  TALOS_TEST_STRINGIFY(__USER_LABEL_PREFIX__) name

extern "C" {
extern uint8_t __talos_state_value[], __talos_state_unknown[];
extern uint8_t __talos_eval_selected_variant_v1_0;
extern uint8_t __talos_eval_step_four_state_fallback_v1;
extern uint64_t __talos_eval_route_promotion_pending_v1[];
uint8_t probe(void *) asm(
    TALOS_TEST_SYMBOL("lane.__talos_eval_body_0.__talos_path_known_0"));
void __talos_eval_path_dispatch_v1_0(void *);
void __talos_eval_route_promotion_scan_v1();
void __talos_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
void __talos_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
int __talos_eval_four_state_nba_handoff_v1(void *, void *, void *);
}

int main() {
  std::memset(__talos_state_value, 0, 4);
  std::memset(__talos_state_unknown, 0, 4);
  __talos_state_unknown[2] = __talos_state_unknown[3] = 0xff;
  __talos_eval_promotion_invalidate_range_v1(16, 16);
  __talos_eval_route_promotion_scan_v1();
  assert(!__talos_eval_selected_variant_v1_0);
  assert(probe(nullptr) == 0);

  // An X/Z reset is not an asserted reset (LRM 12.4), even if its value
  // plane is one. Taking an edge on it is not a known-state certificate.
  __talos_state_value[1] = 1;
  __talos_state_unknown[1] = 1;
  assert(probe(nullptr) == 0);
  __talos_state_unknown[1] = 0;
  assert(probe(nullptr) == 1);
  __talos_eval_step_four_state_fallback_v1 = 0;
  __talos_eval_path_dispatch_v1_0(nullptr);
  assert(!__talos_eval_step_four_state_fallback_v1);
  assert(!__talos_eval_selected_variant_v1_0);
  assert(__talos_state_unknown[2] == 0xff);
  assert(__talos_state_unknown[3] == 0xff);

  // Staging reset does not initialize the state. Promotion becomes persistent
  // only after the actual NBA updates (LRM 4.6 and 10.4.2).
  assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(!__talos_state_unknown[2] && !__talos_state_unknown[3]);
  __talos_eval_route_promotion_scan_v1();
  assert(__talos_eval_selected_variant_v1_0);
  __talos_state_value[1] = 0;
  for (unsigned tick = 1; tick != 8; ++tick) {
    __talos_eval_step_four_state_fallback_v1 = 0;
    __talos_eval_path_dispatch_v1_0(nullptr);
    assert(!__talos_eval_step_four_state_fallback_v1);
    assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr,
                                                    nullptr) == 0);
    assert(__talos_state_value[2] == tick);
    assert(__talos_state_value[3] == tick);
    assert(!__talos_eval_route_promotion_pending_v1[0]);
  }

  // The reset input participates in the persistent certificate too. An
  // unknown reset revokes it; the four-state if then takes the ordinary arm.
  __talos_state_value[1] = 1;
  __talos_state_unknown[1] = 1;
  __talos_eval_promotion_invalidate_range_v1(8, 8);
  assert(!__talos_eval_selected_variant_v1_0);
  assert(probe(nullptr) == 0);
  __talos_eval_step_four_state_fallback_v1 = 0;
  __talos_eval_path_dispatch_v1_0(nullptr);
  assert(__talos_eval_step_four_state_fallback_v1);
  assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__talos_state_value[2] == 8 && __talos_state_value[3] == 8);
  __talos_eval_route_promotion_scan_v1();
  assert(!__talos_eval_selected_variant_v1_0);
  __talos_state_value[1] = __talos_state_unknown[1] = 0;
  __talos_eval_promotion_recheck_range_v1(8, 8);
  __talos_eval_route_promotion_scan_v1();
  assert(__talos_eval_selected_variant_v1_0);

  // One unknown register revokes the entire lane, including reads of the
  // otherwise known register. Restoring only its neighbors cannot promote it.
  __talos_state_unknown[3] = 0x80;
  __talos_eval_promotion_invalidate_range_v1(31, 1);
  assert(!__talos_eval_selected_variant_v1_0);
  __talos_eval_route_promotion_scan_v1();
  assert(!__talos_eval_selected_variant_v1_0);
  assert(probe(nullptr) == 0);
  __talos_eval_promotion_recheck_range_v1(16, 8);
  __talos_eval_route_promotion_scan_v1();
  assert(!__talos_eval_selected_variant_v1_0);
  __talos_eval_step_four_state_fallback_v1 = 0;
  __talos_eval_path_dispatch_v1_0(nullptr);
  assert(__talos_eval_step_four_state_fallback_v1);
  assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__talos_state_unknown[3] == 0xff);
  __talos_state_value[1] = 1;
  __talos_eval_path_dispatch_v1_0(nullptr);
  assert(__talos_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  __talos_eval_route_promotion_scan_v1();
  assert(__talos_eval_selected_variant_v1_0);
  assert(!__talos_state_unknown[2] && !__talos_state_unknown[3]);
  std::puts("reset lane promotion passed");
}
