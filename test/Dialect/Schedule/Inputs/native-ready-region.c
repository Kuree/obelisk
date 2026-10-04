#include <assert.h>
#include <stdint.h>
#include <string.h>

// Execution harness for the MLIR lowering test; no VPI or DPI integration.
extern uint64_t ready[66];
uint64_t ordinary, seen_before, seen_after;
static uint64_t expected;
void exercise(uint64_t, uint64_t *);
void alias_updates(void);
void observe(uint64_t n) {
  assert(ready[0] == expected);
  assert(ready[63] == (4 | (n ? 1ull << 63 : 0)));
  assert(ready[64] == 0 && ready[65] == (1 | (1ull << 63)));
  ready[0] |= 1ull << 16;
  ready[63] ^= 4;
  if (!ready[63])
    ready[65] &= ~(1ull << 63);
}
int main(void) {
  alias_updates();
  assert(ready[0] == 2);
  for (unsigned n = 0; n <= 8; ++n) {
    memset(ready, 0, sizeof(ready));
    ready[0] = expected = 1ull << 40;
    ready[63] = 4;
    ready[65] = 1 | (1ull << 63);
    ordinary = 99;
    for (unsigned i = 0; i < n; ++i) {
      expected |= 1ull << i;
      expected = i & 1 ? expected | 1 : expected & ~1ull;
    }
    exercise(n, ready);
    assert(seen_before == expected);
    assert(seen_after == (expected | (1ull << 16) | 256));
    assert(ready[0] == (expected | 256 | (1ull << 32)));
    assert(ready[63] == 0 && ready[64] == 0 && ready[65] == 1);
    assert(ordinary == (n ? n - 1 : 99));
  }
  return 0;
}
