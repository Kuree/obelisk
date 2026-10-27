#include "talos/Runtime/Runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
extern uint8_t __talos_state_value[], __talos_state_unknown[];
extern uint8_t __talos_eval_kernel_promotion_latched_v1[];
extern uint8_t __talos_eval_group_promotion_latched_v1[];
extern uint8_t __talos_eval_step_four_state_fallback_v1;
extern uint64_t __talos_eval_promotion_pending_mask_v1[];
extern uint64_t __talos_aot_model_ingress_v1[];
void __talos_eval_ranked_group_0(void *);
void __talos_eval_promotion_invalidate_v1();
void __talos_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
void __talos_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
int model_main(int, char **);
talos_rt_status __real_talos_rt_v1_scheduler_install_aot(
    talos_rt_context *, const talos_rt_native_schedule_plan *);
void __real_talos_rt_v1_context_destroy(talos_rt_context *);
}

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "generated group proof failure at line %d\n",       \
                   __LINE__);                                                  \
      std::abort();                                                            \
    }                                                                          \
  } while (false)

constexpr uint64_t membersMask = 510;
constexpr uint64_t lastOwner = uint64_t{1} << 8;
static unsigned installs = 0, destroys = 0;

static void checkReset() {
  CHECK(__talos_eval_group_promotion_latched_v1[0] == 0);
  CHECK(__talos_eval_promotion_pending_mask_v1[0] == membersMask);
  for (unsigned owner = 0; owner != 9; ++owner)
    CHECK(__talos_eval_kernel_promotion_latched_v1[owner] == 0);
}

// Exercise the actual generated plan hooks used by installation and release.
// The main's native state initialization must not stand in for proof reset.
extern "C" talos_rt_status __wrap_talos_rt_v1_scheduler_install_aot(
    talos_rt_context *context, const talos_rt_native_schedule_plan *plan) {
  CHECK(__talos_eval_group_promotion_latched_v1[0] == 1);
  auto status = __real_talos_rt_v1_scheduler_install_aot(context, plan);
  CHECK(status == TALOS_RT_OK);
  checkReset();
  ++installs;
  return status;
}
extern "C" void
__wrap_talos_rt_v1_context_destroy(talos_rt_context *context) {
  // The final source values are known. Leave a positive result for release to
  // revoke, regardless of the final observer's preceding checkpoint behavior.
  __talos_eval_group_promotion_latched_v1[0] = 1;
  __real_talos_rt_v1_context_destroy(context);
  checkReset();
  ++destroys;
}

static void prepareKnown() {
  // Real layout: source [8,16), eight outputs [16,80), counter [80,88).
  // The seven inactive outputs are coherent with their unchanged inputs.
  // Only the last owner is activated to repair its deliberately stale output.
  std::memset(__talos_state_value, 0, 19);
  std::memset(__talos_state_unknown, 0, 19);
  for (unsigned byte = 1; byte != 10; ++byte)
    __talos_state_value[byte] = byte - 1;
  __talos_state_value[9] = 99;
  __talos_eval_promotion_invalidate_v1();
  __talos_aot_model_ingress_v1[0] = lastOwner;
  __talos_eval_step_four_state_fallback_v1 = 0;
  checkReset();
}

int main() {
  prepareKnown();
  __talos_eval_ranked_group_0(nullptr);
  CHECK(__talos_state_value[9] == 8 && __talos_state_unknown[9] == 0);
  CHECK(__talos_eval_group_promotion_latched_v1[0] == 1);
  CHECK(__talos_eval_promotion_pending_mask_v1[0] == membersMask);
  CHECK(__talos_aot_model_ingress_v1[0] == 0);
  for (unsigned owner = 0; owner != 9; ++owner)
    CHECK(__talos_eval_kernel_promotion_latched_v1[owner] == 0);
  // The unchanged ordinary group would have queried the active member and
  // cleared its pending bit. This proves actual fast-body reachability with
  // seven inactive members whose own proof remains unqueried/pending.

  prepareKnown();
  __talos_state_unknown[2] = 0x80;
  __talos_eval_promotion_invalidate_range_v1(16, 8);
  __talos_eval_ranked_group_0(nullptr);
  CHECK(__talos_state_value[9] == 8 && __talos_state_unknown[9] == 0);
  CHECK(__talos_state_unknown[2] == 0x80);
  CHECK(__talos_eval_group_promotion_latched_v1[0] == 0);
  CHECK(__talos_eval_kernel_promotion_latched_v1[8] == 1);
  CHECK(__talos_eval_promotion_pending_mask_v1[0] ==
        (membersMask & ~lastOwner));
  for (unsigned owner = 1; owner != 8; ++owner)
    CHECK(__talos_eval_kernel_promotion_latched_v1[owner] == 0);
  // A failed group proof executes only the actual pending member, which may
  // still prove its own narrower inputs. It does not activate the X producer.

  prepareKnown();
  __talos_state_unknown[8] = 1;
  __talos_eval_promotion_invalidate_range_v1(64, 8);
  __talos_eval_ranked_group_0(nullptr);
  CHECK(__talos_eval_step_four_state_fallback_v1 == 1);
  CHECK(__talos_state_unknown[9] == 255);
  CHECK(__talos_eval_group_promotion_latched_v1[0] == 0);
  __talos_state_unknown[8] = 0;
  __talos_eval_promotion_recheck_range_v1(64, 8);
  __talos_aot_model_ingress_v1[0] = lastOwner;
  __talos_eval_step_four_state_fallback_v1 = 0;
  __talos_eval_ranked_group_0(nullptr);
  CHECK(__talos_eval_step_four_state_fallback_v1 == 1);
  CHECK(__talos_state_unknown[9] == 0); // old destination also needs proof
  __talos_aot_model_ingress_v1[0] = lastOwner;
  __talos_eval_ranked_group_0(nullptr);
  CHECK(__talos_eval_group_promotion_latched_v1[0] == 1);
  CHECK(__talos_eval_kernel_promotion_latched_v1[8] == 0);
  CHECK(__talos_eval_promotion_pending_mask_v1[0] == membersMask);

  // Use the emitted full reset, independently of any overlap query. No
  // matching kernel invalidation may be required for clearing group positives.
  std::memset(__talos_eval_kernel_promotion_latched_v1, 1, 9);
  __talos_eval_promotion_pending_mask_v1[0] = 0;
  __talos_eval_promotion_invalidate_v1();
  checkReset();

  char name[] = "group-proof-domain";
  char *argv[] = {name, nullptr};
  for (unsigned invocation = 0; invocation != 2; ++invocation) {
    __talos_eval_group_promotion_latched_v1[0] = 1;
    std::memset(__talos_eval_kernel_promotion_latched_v1, 1, 9);
    __talos_eval_promotion_pending_mask_v1[0] = 0;
    CHECK(model_main(1, argv) == 0);
    checkReset();
  }
  CHECK(installs == 2 && destroys == 2);
  std::puts("generated group domain integration passed");
}
