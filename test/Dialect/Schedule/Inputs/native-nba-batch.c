#include <assert.h>
#include <stdint.h>
#include <string.h>

uint8_t plan, value[40], unknown[40];
uint64_t dirty[2], summary, ingress[8];
struct {
  uint64_t value[4], unknown[4], mask[4];
  uint32_t valid, region;
  uint64_t transient[4];
} accumulator;
int64_t offset;
uint64_t staged_value, staged_unknown;
uint32_t valid;
static uint64_t stages, commits, notifications;
static unsigned allowed = 1, pending;
void specialized(void *, uint32_t, uint32_t *);
uint32_t obelisk_rt_v1_static_nba_batch_guard(void *context, const void *p,
                                              uint32_t region) {
  assert(p == &plan);
  return allowed;
}
void obelisk_rt_v1_static_nba_batch_knownness(void *context, uint64_t low,
                                              uint64_t width,
                                              uint64_t old_unknown,
                                              uint64_t new_unknown) {
  assert(width > 0 && width <= 64 && low + width <= 256);
  assert(old_unknown != new_unknown);
  notifications++;
}
void obelisk_rt_v1_static_nba_batch_finish(void *context, uint64_t s,
                                           uint64_t c, uint32_t p) {
  stages += s;
  commits += c;
  pending |= p;
}
static void set(uint8_t *plane, unsigned bit, unsigned next) {
  plane[bit / 8] = (plane[bit / 8] & ~(1u << (bit % 8))) | (next << (bit % 8));
}
static void reset(void) {
  memset(value, 0xa5, sizeof(value));
  memset(unknown, 0, sizeof(unknown));
  memset(&accumulator, 0, sizeof(accumulator));
  memset(ingress, 0, sizeof(ingress));
  dirty[0] = (1ull << 1) | (1ull << 9);
  dirty[1] = 1ull << 1;
  summary = 3;
  stages = commits = notifications = pending = 0;
}
int main(void) {
  reset();
  uint8_t expected[40], expected_unknown[40];
  memcpy(expected, value, sizeof(value));
  memcpy(expected_unknown, unknown, sizeof(unknown));
  accumulator.valid = 1;
  accumulator.region = 2;
  accumulator.value[0] = 0x555;
  accumulator.unknown[0] = 0x111;
  accumulator.mask[0] = 0xfff;
  accumulator.mask[1] = 4;
  accumulator.transient[1] = 4;
  accumulator.value[1] = (value[8] >> 3) & 4;
  accumulator.mask[2] = 1;
  accumulator.value[2] = 1;
  for (unsigned bit = 0; bit < 129; ++bit) {
    unsigned word = bit / 64, shift = bit % 64;
    if ((accumulator.mask[word] >> shift) & 1) {
      set(expected, 3 + bit, (accumulator.value[word] >> shift) & 1);
      set(expected_unknown, 3 + bit, (accumulator.unknown[word] >> shift) & 1);
    }
  }
  valid = 0;
  uint32_t changed = 8;
  specialized(0, 2, &changed);
  assert(!memcmp(value, expected, sizeof(value)));
  assert(!memcmp(unknown, expected_unknown, sizeof(unknown)));
  assert(accumulator.valid == 0 && accumulator.mask[0] == 0 &&
         accumulator.mask[2] == 0);
  assert(accumulator.transient[1] == 0 && ingress[2] == 2 && pending);
  assert(dirty[0] == (1ull << 9) && dirty[1] == 0 && summary == 1);
  assert(changed == 9 && commits == 1 && stages == 3 && notifications == 1);

  // Signed out-of-range/partial targets, unaligned packed planes, and a
  // coincident accumulator for a different event region.
  const int64_t offsets[] = {-8, -64, 0, 63, 72, 80, INT64_MIN, INT64_MAX};
  for (unsigned test = 0; test < sizeof(offsets) / sizeof(*offsets); ++test) {
    reset();
    memcpy(expected, value, sizeof(value));
    memcpy(expected_unknown, unknown, sizeof(unknown));
    accumulator.valid = 1;
    accumulator.region = 4;
    valid = 1;
    offset = offsets[test];
    staged_value = 0xfedcba9876543210ull;
    staged_unknown = 0x8000400020001000ull;
    uint64_t source = offset < 0 ? 0ull - (uint64_t)offset : 0;
    uint64_t dest = offset < 0 ? 0 : (uint64_t)offset;
    unsigned writes = source < 64 && dest < 80;
    if (writes) {
      uint64_t width = 64 - source < 80 - dest ? 64 - source : 80 - dest;
      for (unsigned bit = 0; bit < width; ++bit) {
        set(expected, 144 + dest + bit, (staged_value >> (source + bit)) & 1);
        set(expected_unknown, 144 + dest + bit,
            (staged_unknown >> (source + bit)) & 1);
      }
    }
    unsigned observed = 0;
    for (unsigned bit = 144 + 75; bit < 144 + 80; ++bit)
      observed |= (((expected[bit / 8] ^ value[bit / 8]) |
                    (expected_unknown[bit / 8] ^ unknown[bit / 8])) >>
                   (bit % 8)) &
                  1;
    changed = 0;
    specialized(0, 2, &changed);
    assert(!memcmp(value, expected, sizeof(value)));
    assert(!memcmp(unknown, expected_unknown, sizeof(unknown)));
    assert(!valid && accumulator.valid && accumulator.region == 4);
    assert(dirty[0] == ((1ull << 1) | (1ull << 9)) && !dirty[1] &&
           summary == 1);
    assert(commits == writes && stages == writes && pending == observed);
    assert(ingress[1] == observed && ingress[2] == 0);
  }
  // Guard failure consumes neither a payload nor its dirty/summary bits.
  reset();
  allowed = 0;
  valid = 1;
  changed = 8;
  accumulator.valid = 1;
  accumulator.region = 2;
  specialized(0, 2, &changed);
  assert(valid && accumulator.valid && dirty[0] == 514 && dirty[1] == 2);
  assert(summary == 3 && changed == 8 && !stages && !commits);
  return 0;
}
