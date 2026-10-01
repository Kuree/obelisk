#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t __obelisk_state_value[], __obelisk_state_unknown[];
extern uint8_t __obelisk_eval_selected_variant_v1_0;
extern uint8_t __obelisk_eval_step_four_state_fallback_v1;
extern uint64_t __obelisk_eval_route_promotion_pending_v1[];
uint8_t probe(void *) asm("lane.__obelisk_eval_body_0.__obelisk_path_known_0");
void __obelisk_eval_path_dispatch_v1_0(void *);
void __obelisk_eval_route_promotion_scan_v1();
void __obelisk_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
void __obelisk_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
int __obelisk_eval_four_state_nba_handoff_v1(void *, void *, void *);
}

int main() {
  std::memset(__obelisk_state_value, 0, 4);
  std::memset(__obelisk_state_unknown, 0, 4);
  __obelisk_state_unknown[2] = __obelisk_state_unknown[3] = 0xff;
  __obelisk_eval_promotion_invalidate_range_v1(16, 16);
  __obelisk_eval_route_promotion_scan_v1();
  assert(!__obelisk_eval_selected_variant_v1_0);
  assert(probe(nullptr) == 0);

  // An X/Z reset is not an asserted reset (LRM 12.4), even if its value
  // plane is one. Taking an edge on it is not a known-state certificate.
  __obelisk_state_value[1] = 1;
  __obelisk_state_unknown[1] = 1;
  assert(probe(nullptr) == 0);
  __obelisk_state_unknown[1] = 0;
  assert(probe(nullptr) == 1);
  __obelisk_eval_step_four_state_fallback_v1 = 0;
  __obelisk_eval_path_dispatch_v1_0(nullptr);
  assert(!__obelisk_eval_step_four_state_fallback_v1);
  assert(!__obelisk_eval_selected_variant_v1_0);
  assert(__obelisk_state_unknown[2] == 0xff);
  assert(__obelisk_state_unknown[3] == 0xff);

  // Staging reset does not initialize the state. Promotion becomes persistent
  // only after the actual NBA updates (LRM 4.6 and 10.4.2).
  assert(__obelisk_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(!__obelisk_state_unknown[2] && !__obelisk_state_unknown[3]);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_selected_variant_v1_0);
  __obelisk_state_value[1] = 0;
  for (unsigned tick = 1; tick != 8; ++tick) {
    __obelisk_eval_step_four_state_fallback_v1 = 0;
    __obelisk_eval_path_dispatch_v1_0(nullptr);
    assert(!__obelisk_eval_step_four_state_fallback_v1);
    assert(__obelisk_eval_four_state_nba_handoff_v1(nullptr, nullptr,
                                                    nullptr) == 0);
    assert(__obelisk_state_value[2] == tick);
    assert(__obelisk_state_value[3] == tick);
    assert(!__obelisk_eval_route_promotion_pending_v1[0]);
  }

  // The reset input participates in the persistent certificate too. An
  // unknown reset revokes it; the four-state if then takes the ordinary arm.
  __obelisk_state_value[1] = 1;
  __obelisk_state_unknown[1] = 1;
  __obelisk_eval_promotion_invalidate_range_v1(8, 8);
  assert(!__obelisk_eval_selected_variant_v1_0);
  assert(probe(nullptr) == 0);
  __obelisk_eval_step_four_state_fallback_v1 = 0;
  __obelisk_eval_path_dispatch_v1_0(nullptr);
  assert(__obelisk_eval_step_four_state_fallback_v1);
  assert(__obelisk_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__obelisk_state_value[2] == 8 && __obelisk_state_value[3] == 8);
  __obelisk_eval_route_promotion_scan_v1();
  assert(!__obelisk_eval_selected_variant_v1_0);
  __obelisk_state_value[1] = __obelisk_state_unknown[1] = 0;
  __obelisk_eval_promotion_recheck_range_v1(8, 8);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_selected_variant_v1_0);

  // One unknown register revokes the entire lane, including reads of the
  // otherwise known register. Restoring only its neighbors cannot promote it.
  __obelisk_state_unknown[3] = 0x80;
  __obelisk_eval_promotion_invalidate_range_v1(31, 1);
  assert(!__obelisk_eval_selected_variant_v1_0);
  __obelisk_eval_route_promotion_scan_v1();
  assert(!__obelisk_eval_selected_variant_v1_0);
  assert(probe(nullptr) == 0);
  __obelisk_eval_promotion_recheck_range_v1(16, 8);
  __obelisk_eval_route_promotion_scan_v1();
  assert(!__obelisk_eval_selected_variant_v1_0);
  __obelisk_eval_step_four_state_fallback_v1 = 0;
  __obelisk_eval_path_dispatch_v1_0(nullptr);
  assert(__obelisk_eval_step_four_state_fallback_v1);
  assert(__obelisk_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  assert(__obelisk_state_unknown[3] == 0xff);
  __obelisk_state_value[1] = 1;
  __obelisk_eval_path_dispatch_v1_0(nullptr);
  assert(__obelisk_eval_four_state_nba_handoff_v1(nullptr, nullptr, nullptr) ==
         0);
  __obelisk_eval_route_promotion_scan_v1();
  assert(__obelisk_eval_selected_variant_v1_0);
  assert(!__obelisk_state_unknown[2] && !__obelisk_state_unknown[3]);
  std::puts("reset lane promotion passed");
}
