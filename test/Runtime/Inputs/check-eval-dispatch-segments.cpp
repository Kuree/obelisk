#include "obelisk/Runtime/EvalNBAQueue.h"
#include "obelisk/Runtime/ReadySet.h"
#include "obelisk/Runtime/Runtime.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>

// asm labels are final symbol names; Mach-O prefixes C symbols with '_'.
#define OBELISK_TEST_STRINGIFY_(text) #text
#define OBELISK_TEST_STRINGIFY(text) OBELISK_TEST_STRINGIFY_(text)
#define OBELISK_TEST_SYMBOL(name)                                              \
  OBELISK_TEST_STRINGIFY(__USER_LABEL_PREFIX__) name

extern "C" {
extern uint64_t __obelisk_aot_model_ingress_v1[];
extern uint32_t *__obelisk_periodic_termination_v1;
int segment0(obelisk_rt_context *, uint64_t, bool *) asm(
    OBELISK_TEST_SYMBOL("__obelisk_eval_dispatch_v1.segment_0"));
int segment1(obelisk_rt_context *, uint64_t, bool *) asm(
    OBELISK_TEST_SYMBOL("__obelisk_eval_dispatch_v1.segment_1"));
#ifdef ORDERED_NBA
extern obelisk::runtime::EvalNBAQueue __obelisk_eval_ordered_nba_queue_v1;
int __obelisk_eval_dispatch_v1(void *, obelisk_rt_context *);
int model_main(int, char **);
#endif
}

int main() {
  obelisk_rt_context *context = nullptr;
  assert(obelisk_rt_v1_context_create(&context) == OBELISK_RT_OK);
  auto *ingress = __obelisk_aot_model_ingress_v1;
#ifdef ORDERED_NBA
  const obelisk::runtime::ReadySetLayout layout(517);
#else
  const obelisk::runtime::ReadySetLayout layout(514);
#endif
  obelisk::runtime::ReadySetView ready(ingress, layout);
  bool fallback = false;
  auto reset = [&] {
    std::fill_n(ingress, layout.storageWords, uint64_t{0});
    ingress[layout.cacheOffset()] = layout.counts[0];
    __obelisk_periodic_termination_v1 = nullptr;
    fallback = false;
  };

#ifdef ORDERED_NBA
  // Model a queue failure recorded by the just-executed owner. Even though
  // another owner is ready in the same leaf, return before executing it and
  // let the coordinator propagate the exact status without committing NBAs.
  reset();
  ready.set(0);
  ready.set(1);
  auto &queue = __obelisk_eval_ordered_nba_queue_v1;
  queue.error = OBELISK_RT_OUT_OF_RESOURCES;
  assert(segment0(context, 0, &fallback) == OBELISK_RT_OK);
  assert(!ready.test(0) && ready.test(1));
  assert(__obelisk_eval_dispatch_v1(nullptr, context) ==
         OBELISK_RT_OUT_OF_RESOURCES);
  assert(ready.test(1) && queue.size == 0);
  queue.error = OBELISK_RT_OK;
  std::puts("ordered NBA error handoff passed");
#else
  // Both real owners in the partial last segment run before returning. The
  // resulting empty leaf must not feed a zero-poison cttz.
  reset();
  ready.set(512);
  ready.set(513);
  assert(segment1(context, 512, &fallback) == OBELISK_RT_OK);
  assert(!ready.test(512) && !ready.test(513));

  // A stale lower bound, the empty sentinel, and a far-out cached index must
  // all hand off before the dynamic leaf load, leaving other work pending.
  for (uint64_t cached : {uint64_t{0}, uint64_t{9}, UINT64_MAX}) {
    reset();
    ready.set(512);
    ready.set(513);
    ingress[layout.cacheOffset()] = cached;
    assert(segment1(context, 512, &fallback) == OBELISK_RT_OK);
    assert(!ready.test(512) && ready.test(513));
  }

  // Empty cached leaves inside the segment also return to the outer scanner.
  reset();
  ready.set(1);
  ready.set(64);
  assert(segment0(context, 1, &fallback) == OBELISK_RT_OK);
  assert(!ready.test(1) && ready.test(64));

  // Seed lower ready bits as if the selected owner had published them. The
  // next selection must reload the global minimum instead of advancing a
  // segment-local cursor past this backward work.
  reset();
  ready.set(1);
  ready.set(2);
  ready.set(3);
  assert(segment0(context, 3, &fallback) == OBELISK_RT_OK);
  assert(ingress[0] == 0);

  // Bit zero belongs to a ranked group. A local encounter hands it back
  // untouched, together with later direct work; an initial call rejects it.
  reset();
  ready.set(0);
  ready.set(1);
  ready.set(2);
  assert(segment0(context, 2, &fallback) == OBELISK_RT_OK);
  assert(ready.test(0) && ready.test(1) && !ready.test(2));
  assert(segment0(context, 0, &fallback) == OBELISK_RT_TIER_UNAVAILABLE);
  assert(ready.test(0) && ready.test(1));
  assert(segment0(context, 512, &fallback) == OBELISK_RT_TIER_UNAVAILABLE);
  assert(segment1(context, 514, &fallback) == OBELISK_RT_TIER_UNAVAILABLE);

  // A stop request ends the batch after the current owner, even when the
  // next ready owner is in the same word and the local range check succeeds.
  reset();
  uint32_t stopping = 1;
  __obelisk_periodic_termination_v1 = &stopping;
  ready.set(1);
  ready.set(2);
  assert(segment0(context, 1, &fallback) == OBELISK_RT_TIER_UNAVAILABLE);
  assert(!ready.test(1) && ready.test(2));
  __obelisk_periodic_termination_v1 = nullptr;
  std::puts("segment ready selection and handoffs passed");
#endif
  obelisk_rt_v1_context_destroy(context);
#ifdef ORDERED_NBA
  // Execute the complete MLIR model as well: the wide NBA root makes a
  // 0 -> 1 -> 0 round trip and its posedge observer must still fire.
  reset();
  char name[] = "segmented-ordered-nba";
  char *argv[] = {name, nullptr};
  assert(model_main(1, argv) == 0);
#endif
}
