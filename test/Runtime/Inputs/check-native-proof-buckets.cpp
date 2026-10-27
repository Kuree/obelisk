#include <cstdint>
#include <cstdio>

extern "C" {
void proof_reset();
uint64_t proof_known();
uint8_t proof_latched();
uint8_t proof_dirty();
void __talos_eval_promotion_invalidate_range_v1(uint64_t, uint64_t);
void __talos_eval_promotion_recheck_range_v1(uint64_t, uint64_t);
}

int main() {
  const uint64_t offsets[] = {0,   63,  64,  65,  66,  127,  128,       129,
                              191, 256, 959, 960, 961, 1024, UINT64_MAX};
  const uint64_t widths[] = {0, 1, 2, 63, 64, 65, 127, UINT64_MAX};
  for (uint64_t offset : offsets)
    for (uint64_t width : widths) {
      uint64_t end = width > UINT64_MAX - offset ? UINT64_MAX : offset + width;
      auto overlaps = [&](uint64_t begin, uint64_t stop) {
        return width && begin < end && offset < stop;
      };
      bool first = false;
      for (uint64_t index = 0; index != 8; ++index)
        first |= overlaps(index * 128 + 63, index * 128 + 65);
      bool second = overlaps(64, 66);
      uint64_t known = 3 & ~(uint64_t(first) | (uint64_t(second) << 1));
      proof_reset();
      __talos_eval_promotion_invalidate_range_v1(offset, width);
      if (proof_known() != known || proof_latched() != !(first || second) ||
          proof_dirty() != (first || second)) {
        std::fprintf(stderr, "invalidate offset=%llu width=%llu\n",
                     static_cast<unsigned long long>(offset),
                     static_cast<unsigned long long>(width));
        return 1;
      }
      proof_reset();
      __talos_eval_promotion_recheck_range_v1(offset, width);
      if (proof_known() != 3 || proof_latched() != 1 ||
          proof_dirty() != (first || second))
        return 2;
    }
}
