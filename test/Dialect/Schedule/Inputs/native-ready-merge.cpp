#include "obelisk/Runtime/ReadySet.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using obelisk::runtime::ReadySetLayout;
using obelisk::runtime::ReadySetView;
extern "C" {
#define MERGE(N) void merge_##N(uint64_t *, uint64_t *);
MERGE(1)
MERGE(63)
MERGE(64)
MERGE(65)
MERGE(128)
MERGE(2048)
MERGE(2049)
MERGE(4096)
MERGE(262145)
#undef MERGE
uint64_t ready_merge_observed[3];
uint64_t merge_observed(uint64_t *);
void ready_merge_observe(uint64_t *base) {
  if (base[0] != 3 || base[1] != 4 || base[2] != 0)
    std::abort();
  // The later ready update must observe the callback's changes.
  base[0] = 0;
  base[1] = 16;
}
}

struct Test {
  uint32_t capacity;
  void (*merge)(uint64_t *, uint64_t *);
};

int main() {
  const Test tests[] = {
#define MERGE(N) {N, merge_##N},
      MERGE(1) MERGE(63) MERGE(64) MERGE(65) MERGE(128) MERGE(2048) MERGE(2049)
          MERGE(4096) MERGE(262145)
#undef MERGE
  };
  for (auto test : tests) {
    const ReadySetLayout layout(test.capacity);
    // Prefix/suffix canaries also provide valid overlapping source ranges.
    for (unsigned mode = 0; mode != 4; ++mode)
      for (unsigned overlap = 0; overlap != 5; ++overlap) {
        std::vector<uint64_t> actual(2 * layout.storageWords + 8,
                                     UINT64_C(0xfeedfacefeedface));
        uint64_t *base = actual.data() + 1;
        std::fill(base, base + layout.storageWords, 0);
        if (layout.hasCache())
          base[layout.cacheOffset()] = layout.counts[0];
        ReadySetView initial(base, layout);
        if (mode >= 2) {
          // Existing leaves and parents exercise both propagation stop cases,
          // including the 63/64 and 4095/4096 word boundaries.
          for (uint32_t word = mode == 3 ? 1 : 0; word < layout.counts[0];
               word += 67)
            initial.setWord(word, 1);
          for (uint32_t word : {63, 64, 4095, 4096})
            initial.setWord(word, 2);
        }
        if (mode == 3 && layout.hasCache()) {
          if (base[0] != 0 || base[1] == 0)
            std::abort();
          base[layout.cacheOffset()] =
              0; // Strictly below the first ready leaf.
        }
        size_t source = layout.storageWords + 2;
        for (uint32_t word = 0; word < layout.counts[0]; ++word)
          actual[source + word] = mode == 0 || mode == 3 ? 0
                                  : mode == 1
                                      ? (word % 71 == 0 ? UINT64_MAX : 0)
                                      : UINT64_MAX;
        if (overlap == 1)
          source = 1; // source == destination
        if (overlap == 2)
          source = 2; // A later source read can observe the updated cache.
        if (overlap == 3) {
          source = 0; // Earlier leaf writes feed the following source reads.
          actual[0] = 1;
        }
        if (overlap == 4)
          source = 3; // A later source read can observe updated summary bits.
        auto expected = actual;
        ReadySetView reference(expected.data() + 1, layout);
        for (uint32_t word = 0; word < layout.counts[0]; ++word)
          reference.setWord(word, expected[source + word]);
        test.merge(base, actual.data() + source);
        // An empty, disjoint merge preserves a genuinely stale-low cache.
        if (mode == 3 && overlap == 0 && layout.hasCache() &&
            (base[0] != 0 || base[1] == 0 || base[layout.cacheOffset()] != 0))
          std::abort();
        if (actual != expected) {
          std::fprintf(stderr, "capacity=%u mode=%u overlap=%u\n",
                       test.capacity, mode, overlap);
          return 1;
        }
      }
  }
  ready_merge_observed[0] = ready_merge_observed[1] = 0;
  ready_merge_observed[2] = 2;
  uint64_t source[] = {2, 4};
  if (merge_observed(source) != 24 || ready_merge_observed[0] != 0 ||
      ready_merge_observed[1] != 24 || ready_merge_observed[2] != 0)
    return 2;
}
