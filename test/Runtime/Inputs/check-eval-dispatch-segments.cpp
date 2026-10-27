#include "talos/Runtime/EvalNBAQueue.h"
#include "talos/Runtime/ReadySet.h"
#include "talos/Runtime/Runtime.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>

// asm labels are final symbol names; Mach-O prefixes C symbols with '_'.
#define TALOS_TEST_STRINGIFY_(text) #text
#define TALOS_TEST_STRINGIFY(text) TALOS_TEST_STRINGIFY_(text)
#define TALOS_TEST_SYMBOL(name)                                              \
  TALOS_TEST_STRINGIFY(__USER_LABEL_PREFIX__) name

extern "C" {
extern uint64_t __talos_aot_model_ingress_v1[];
extern uint32_t *__talos_periodic_termination_v1;
int segment0(talos_rt_context *, uint64_t, bool *) asm(
    TALOS_TEST_SYMBOL("__talos_eval_dispatch_v1.segment_0"));
int segment1(talos_rt_context *, uint64_t, bool *) asm(
    TALOS_TEST_SYMBOL("__talos_eval_dispatch_v1.segment_1"));
#ifdef ORDERED_NBA
extern talos::runtime::EvalNBAQueue __talos_eval_ordered_nba_queue_v1;
int __talos_eval_dispatch_v1(void *, talos_rt_context *);
int model_main(int, char **);
#endif
}

int main() {
  talos_rt_context *context = nullptr;
  assert(talos_rt_v1_context_create(&context) == TALOS_RT_OK);
  auto *ingress = __talos_aot_model_ingress_v1;
#ifdef ORDERED_NBA
  const talos::runtime::ReadySetLayout layout(517);
#else
  const talos::runtime::ReadySetLayout layout(514);
#endif
  talos::runtime::ReadySetView ready(ingress, layout);
  bool fallback = false;
  auto reset = [&] {
    std::fill_n(ingress, layout.storageWords, uint64_t{0});
    ingress[layout.cacheOffset()] = layout.counts[0];
    __talos_periodic_termination_v1 = nullptr;
    fallback = false;
  };

#ifdef ORDERED_NBA
  // Model a queue failure recorded by the just-executed owner. Even though
  // another owner is ready in the same leaf, return before executing it and
  // let the coordinator propagate the exact status without committing NBAs.
  reset();
  ready.set(0);
  ready.set(1);
  auto &queue = __talos_eval_ordered_nba_queue_v1;
  queue.error = TALOS_RT_OUT_OF_RESOURCES;
  assert(segment0(context, 0, &fallback) == TALOS_RT_OK);
  assert(!ready.test(0) && ready.test(1));
  assert(__talos_eval_dispatch_v1(nullptr, context) ==
         TALOS_RT_OUT_OF_RESOURCES);
  assert(ready.test(1) && queue.size == 0);
  queue.error = TALOS_RT_OK;
  std::puts("ordered NBA error handoff passed");
#else
  // Both real owners in the partial last segment run before returning. The
  // resulting empty leaf must not feed a zero-poison cttz.
  reset();
  ready.set(512);
  ready.set(513);
  assert(segment1(context, 512, &fallback) == TALOS_RT_OK);
  assert(!ready.test(512) && !ready.test(513));

  // A stale lower bound, the empty sentinel, and a far-out cached index must
  // all hand off before the dynamic leaf load, leaving other work pending.
  for (uint64_t cached : {uint64_t{0}, uint64_t{9}, UINT64_MAX}) {
    reset();
    ready.set(512);
    ready.set(513);
    ingress[layout.cacheOffset()] = cached;
    assert(segment1(context, 512, &fallback) == TALOS_RT_OK);
    assert(!ready.test(512) && ready.test(513));
  }

  // Empty cached leaves inside the segment also return to the outer scanner.
  reset();
  ready.set(1);
  ready.set(64);
  assert(segment0(context, 1, &fallback) == TALOS_RT_OK);
  assert(!ready.test(1) && ready.test(64));

  // Seed lower ready bits as if the selected owner had published them. The
  // next selection must reload the global minimum instead of advancing a
  // segment-local cursor past this backward work.
  reset();
  ready.set(1);
  ready.set(2);
  ready.set(3);
  assert(segment0(context, 3, &fallback) == TALOS_RT_OK);
  assert(ingress[0] == 0);

  // Bit zero belongs to a ranked group. A local encounter hands it back
  // untouched, together with later direct work; an initial call rejects it.
  reset();
  ready.set(0);
  ready.set(1);
  ready.set(2);
  assert(segment0(context, 2, &fallback) == TALOS_RT_OK);
  assert(ready.test(0) && ready.test(1) && !ready.test(2));
  assert(segment0(context, 0, &fallback) == TALOS_RT_TIER_UNAVAILABLE);
  assert(ready.test(0) && ready.test(1));
  assert(segment0(context, 512, &fallback) == TALOS_RT_TIER_UNAVAILABLE);
  assert(segment1(context, 514, &fallback) == TALOS_RT_TIER_UNAVAILABLE);

  // A stop request ends the batch after the current owner, even when the
  // next ready owner is in the same word and the local range check succeeds.
  reset();
  uint32_t stopping = 1;
  __talos_periodic_termination_v1 = &stopping;
  ready.set(1);
  ready.set(2);
  assert(segment0(context, 1, &fallback) == TALOS_RT_TIER_UNAVAILABLE);
  assert(!ready.test(1) && ready.test(2));
  __talos_periodic_termination_v1 = nullptr;
  std::puts("segment ready selection and handoffs passed");
#endif
  talos_rt_v1_context_destroy(context);
#ifdef ORDERED_NBA
  // Execute the complete MLIR model as well: the wide NBA root makes a
  // 0 -> 1 -> 0 round trip and its posedge observer must still fire.
  reset();
  char name[] = "segmented-ordered-nba";
  char *argv[] = {name, nullptr};
  assert(model_main(1, argv) == 0);
#endif
}
