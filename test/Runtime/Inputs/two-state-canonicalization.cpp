#include <cstdint>
#include <cstdio>
extern "C" uint64_t arithmetic(uint8_t, uint8_t);
extern "C" uint64_t reference_arithmetic(uint8_t, uint8_t);
extern "C" uint32_t comparisons(uint8_t, uint8_t);
extern "C" uint32_t reference_comparisons(uint8_t, uint8_t);
extern "C" uint64_t parameter(uint8_t);
extern "C" uint64_t reference_parameter(uint8_t);
int main() {
  for (unsigned a = 0; a < 256; ++a) {
    auto expected = uint64_t(int64_t(int8_t(uint8_t(a + 2))));
    if (parameter(a) != expected || reference_parameter(a) != expected)
      return 1;
    for (unsigned b = 0; b < 256; ++b)
      if (arithmetic(a, b) != reference_arithmetic(a, b) ||
          comparisons(a, b) != reference_comparisons(a, b)) {
        std::printf("mismatch a=%u b=%u\n", a, b);
        return 1;
      }
  }
  std::puts("PASS: 65536 operand pairs and 256 parameter indices");
}
