#include <assert.h>
#include <stdint.h>
#include <string.h>

uint8_t plan;
uint64_t value[5], dirty, summary, ingress;
struct {
  uint64_t value[4], mask[4];
  uint32_t valid, region;
  uint64_t transient[4];
} __obelisk_aot_nba_accumulator_0;
#define accumulator __obelisk_aot_nba_accumulator_0
static uint64_t stages, commits;

void specialized(void *, uint32_t, uint32_t *);
uint32_t obelisk_rt_v1_static_nba_batch_guard(void *context, const void *p,
                                              uint32_t region) {
  assert(p == &plan);
  return 1;
}
void obelisk_rt_v1_static_nba_batch_knownness(void *context, uint64_t low,
                                              uint64_t width, uint64_t old,
                                              uint64_t next) {
  assert(0 && "two-state commits cannot change knownness");
}
void obelisk_rt_v1_static_nba_batch_finish(void *context, uint64_t s,
                                           uint64_t c, uint32_t pending) {
  stages += s;
  commits += c;
  assert(!pending);
}
int main(void) {
  _Static_assert(sizeof(accumulator) == 104, "compact NBA record size");
  dirty = summary = 1;
  accumulator.valid = 1;
  accumulator.region = 6; // Re-NBA, not NBA (2).
  accumulator.value[0] = 7;
  accumulator.mask[0] = 255;
  accumulator.value[3] = 1;
  accumulator.mask[3] = 1;
  uint32_t changed = 0;
  specialized(0, 2, &changed);
  assert(accumulator.valid && dirty == 1 && summary == 1);
  assert(value[0] == 0 && value[3] == 0 && !changed && !commits && !stages);

  // Nonzero transient[3] also must not masquerade as the validity field
  // when the matching barrier consumes this capture.
  accumulator.transient[3] = 1;
  specialized(0, 6, &changed);
  assert(value[0] == 7 && value[3] == 1 && changed == 1);
  assert(!accumulator.valid && !dirty && !summary);
  for (unsigned word = 0; word != 4; ++word)
    assert(!accumulator.mask[word] && !accumulator.transient[word]);
  assert(stages == 2 && commits == 1);
  specialized(0, 6, &changed);
  assert(stages == 2 && commits == 1);
  return 0;
}
