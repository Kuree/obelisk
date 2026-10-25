#include <assert.h>
#include <stdint.h>
#include <string.h>

enum { LEAVES = 4096, CACHE = 4096, SUMMARY = 4097, TOP = 4161, WORDS = 4162 };
static uint64_t expected[WORDS];
uint64_t ready[WORDS];
void consume(uint64_t *, uint64_t);
void consume_structured(uint64_t *, uint64_t, uint64_t);
void consume_global(uint64_t);

// Recompute every summary from the leaves, independently of the incremental
// generated code. Siblings in the same and different summary words exercise
// stopping at each level, including a completely empty ready universe.
static void index_words(uint64_t *words) {
  memset(words + SUMMARY, 0, (WORDS - SUMMARY) * sizeof(*words));
  for (unsigned i = 0; i < LEAVES; ++i)
    if (words[i])
      words[SUMMARY + i / 64] |= UINT64_C(1) << (i % 64);
  for (unsigned i = 0; i < 64; ++i)
    if (words[SUMMARY + i])
      words[TOP] |= UINT64_C(1) << i;
}
void observe(uint64_t *actual) {
  assert(memcmp(actual, expected, sizeof(expected)) == 0);
}
int main(void) {
  static uint64_t actual[WORDS];
  const uint64_t masks[] = {0, 1, 3, UINT64_C(1) << 63, UINT64_MAX};
  for (unsigned i = 0; i < 5; ++i)
    for (unsigned j = 0; j < 5; ++j)
      for (unsigned siblings = 0; siblings < 4; ++siblings) {
        memset(actual, 0, sizeof(actual));
        actual[0] = masks[i];
        actual[1] = siblings & 1;
        actual[64] = siblings & 2;
        index_words(actual);
        memcpy(expected, actual, sizeof(expected));
        expected[0] &= ~masks[j];
        index_words(expected);
        consume(actual, masks[j]);
        // A second consume of the same mask must be harmless, even after
        // the last pending owner in a leaf or summary has disappeared.
        consume(actual, masks[j]);
        memcpy(actual, expected, sizeof(actual));
        actual[0] = masks[i];
        index_words(actual);
        consume_structured(actual, masks[j], 1);
        consume_structured(actual, UINT64_MAX, 0);

        memset(ready, 0, sizeof(ready));
        ready[4095] = masks[i];
        ready[4094] = siblings & 1;
        ready[4031] = siblings & 2;
        ready[CACHE] = siblings & 2 ? 4031 : 4094;
        index_words(ready);
        memcpy(expected, ready, sizeof(expected));
        expected[4095] &= ~masks[j];
        index_words(expected);
        consume_global(masks[j]);
      }
  return 0;
}
