#include <assert.h>
#include <stdint.h>
#include <string.h>

enum { LEAVES = 4096, CACHE = 4096, SUMMARY = 4097, TOP = 4161, WORDS = 4162 };
static uint64_t expected[WORDS];
void exercise(uint64_t *, uint64_t, uint64_t);

// Recompute the oracle's indexes from authoritative leaves. This deliberately
// does not use generated readiness code or incremental summary updates.
static void index_words(uint64_t *words) {
  memset(words + SUMMARY, 0, (WORDS - SUMMARY) * sizeof(*words));
  for (unsigned i = 0; i < LEAVES; ++i)
    if (words[i])
      words[SUMMARY + i / 64] |= UINT64_C(1) << (i % 64);
  for (unsigned i = 0; i < 64; ++i)
    if (words[SUMMARY + i])
      words[TOP] |= UINT64_C(1) << i;
}
static void check(uint64_t *actual) {
  for (unsigned i = 0; i < WORDS; ++i)
    if (i != CACHE)
      assert(actual[i] == expected[i]);
  unsigned first = 0;
  while (first < LEAVES && !actual[first])
    ++first;
  assert(actual[CACHE] <= first);
}
void observe(uint64_t *actual) {
  check(actual);
  actual[64] |= UINT64_C(1) << 60;
  expected[64] = actual[64];
  index_words(actual);
  index_words(expected);
  if (actual[CACHE] > 64)
    actual[CACHE] = 64;
}
int main(void) {
  static uint64_t actual[WORDS];
  const uint64_t masks[] = {0, 1, UINT64_C(1) << 63, UINT64_MAX,
                            UINT64_C(0x5555555555555555)};
  for (unsigned i = 0; i < 5; ++i)
    for (unsigned j = 0; j < 5; ++j) {
      uint64_t a = masks[i], b = masks[j];
      memset(actual, 0, sizeof(actual));
      actual[CACHE] = 64;
      actual[64] = UINT64_MAX;
      actual[4095] = UINT64_MAX;
      actual[127] = 1; // Untouched sibling in a shared summary word.
      index_words(actual);
      memcpy(expected, actual, sizeof(actual));
      expected[0] = b;
      expected[63] = b;
      expected[64] &= ~a;
      expected[4095] = 0;
      index_words(expected);
      exercise(actual, a, b);
      expected[63] = 0;
      expected[4095] = a;
      index_words(expected);
      check(actual);
    }
  return 0;
}
