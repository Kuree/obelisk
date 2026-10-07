#include <cstdint>
#include <cstdio>
#include <initializer_list>

#define UNARY(name)                                                            \
  extern "C" uint64_t name(uint8_t);                                           \
  extern "C" uint64_t reference_##name(uint8_t)
UNARY(assoc0);
UNARY(assoc1);
UNARY(assoc2);
UNARY(assoc3);
UNARY(logical);
UNARY(reduction2);
UNARY(reduction3);
UNARY(resize_conversions);
extern "C" uint64_t multiop(uint8_t, uint8_t);
extern "C" uint64_t reference_multiop(uint8_t, uint8_t);
extern "C" uint64_t mux(uint8_t, uint8_t, uint8_t);
extern "C" uint64_t reference_mux(uint8_t, uint8_t, uint8_t);
extern "C" uint64_t known(uint8_t, uint8_t);
extern "C" uint64_t reference_known(uint8_t, uint8_t);

// Independent LRM truth tables. Symbols are 0, 1, X=2, Z=3. The exported
// byte representation stores value in the low nibble, unknown in the high.
static unsigned symbol(unsigned a, unsigned bit) {
  return ((a >> bit) & 1) | (((a >> (bit + 4)) & 1) << 1);
}
static unsigned binary(unsigned op, unsigned a, unsigned b) {
  if (op < 2) {
    if ((a | b) & 0xf0)
      return 0xf0;
    return (op == 0 ? a + b : a * b) & 15;
  }
  unsigned result = 0;
  for (unsigned bit = 0; bit != 4; ++bit) {
    unsigned x = symbol(a, bit), y = symbol(b, bit), z;
    if (op == 2)
      z = x == 0 || y == 0 ? 0 : x == 1 && y == 1 ? 1 : 2;
    else if (op == 3)
      z = x == 1 || y == 1 ? 1 : x == 0 && y == 0 ? 0 : 2;
    else
      z = x > 1 || y > 1 ? 2 : (x ^ y) ^ (op == 5);
    result |= (z & 1) << bit;
    result |= (z >> 1) << (bit + 4);
  }
  return result;
}
static unsigned truth(unsigned a) {
  if ((a & 15) & ~(a >> 4))
    return 1;
  return a & 0xf0 ? 0x10 : 0;
}
static unsigned reduction(unsigned a, unsigned op, unsigned copies) {
  bool zero = false, one = false, unknown = false;
  unsigned parity = 0;
  for (unsigned bit = 0; bit < copies * 4; ++bit) {
    unsigned s = symbol(a, bit % 4);
    zero |= s == 0;
    one |= s == 1;
    unknown |= s > 1;
    parity ^= s == 1;
  }
  unsigned result = op < 2   ? (zero      ? 0
                                : unknown ? 0x10
                                          : 1)
                    : op < 4 ? (one       ? 1
                                : unknown ? 0x10
                                          : 0)
                             : (unknown ? 0x10 : parity);
  return op & 1 && result < 2 ? result ^ 1 : result;
}
static uint64_t put(uint64_t value, unsigned index, unsigned byte) {
  return value | (uint64_t(byte) << (8 * index));
}
static bool check(const char *name, unsigned a, unsigned b, uint64_t actual,
                  uint64_t reference, uint64_t expected) {
  if (actual == expected && reference == expected)
    return true;
  std::printf("%s a=%u b=%u: canonical=%llx reference=%llx expected=%llx\n",
              name, a, b, (unsigned long long)actual,
              (unsigned long long)reference, (unsigned long long)expected);
  return false;
}
int main() {
  using Fn = uint64_t (*)(uint8_t);
  Fn candidates[] = {assoc0, assoc1, assoc2, assoc3};
  Fn references[] = {reference_assoc0, reference_assoc1, reference_assoc2,
                     reference_assoc3};
  unsigned constants[][2] = {{2, 3}, {0, 15}, {0xa3, 0x5c}, {0xf0, 0xff}};
  for (unsigned a = 0; a < 256; ++a) {
    unsigned converted = (a & 15) & ~(a >> 4);
    unsigned extended = converted | (converted & 8 ? 0xf0 : 0);
    uint64_t resizeExpected =
        extended | (uint64_t(converted) << 8) | (uint64_t(converted & 3) << 16);
    if (!check("resize-conversion", a, 0, resize_conversions(a),
               reference_resize_conversions(a), resizeExpected))
      return 1;
    for (unsigned pair = 0; pair != 4; ++pair) {
      uint64_t expected = 0;
      for (unsigned op = 0; op != 6; ++op)
        expected = put(
            expected, op,
            binary(op, binary(op, a, constants[pair][0]), constants[pair][1]));
      if (!check("associative", a, pair, candidates[pair](a),
                 references[pair](a), expected))
        return 1;
    }
    uint64_t logicalExpected = uint64_t(truth(a)) * 0x01010101 | (1ULL << 32);
    if (!check("logical", a, 0, logical(a), reference_logical(a),
               logicalExpected))
      return 1;
    for (unsigned count : {2u, 3u}) {
      uint64_t expected = 0;
      for (unsigned op = 0; op != 6; ++op)
        expected = put(expected, op, reduction(a, op, count));
      if (!check("reduction", a, count,
                 count == 2 ? reduction2(a) : reduction3(a),
                 count == 2 ? reference_reduction2(a) : reference_reduction3(a),
                 expected))
        return 1;
    }
    for (unsigned b = 0; b != 256; ++b) {
      unsigned count = 0;
      for (unsigned bit = 0; bit != 4; ++bit)
        count += symbol(a, bit) == symbol(b, 0);
      uint64_t multiopExpected = count;
      for (unsigned slot = 1; slot != 5; ++slot)
        multiopExpected = put(multiopExpected, slot, truth(a) == 1);
      if (!check("multiop", a, b, multiop(a, b), reference_multiop(a, b),
                 multiopExpected))
        return 1;
      for (unsigned c = 0; c != 2; ++c) {
        unsigned normalized = (a & 15 & ~(a >> 4)) | (a & 0xf0);
        uint64_t expected = (c ? a : b) | (uint64_t(normalized) << 8) |
                            (uint64_t(a & 15) << 16);
        if (!check("mux", a, b, mux(a, b, c), reference_mux(a, b, c), expected))
          return 1;
      }
      unsigned n = a & 15;
      uint64_t expected =
          ((-n) & 15) | (((~n) & 15) << 8) | (uint64_t(n == 0) << 16);
      for (unsigned op = 0; op != 4; ++op)
        expected = put(expected, op + 3, reduction(n, op, 1));
      expected = put(expected, 7, n != 0 && (b & 15) != 0);
      if (!check("known", a, b, known(a, b), reference_known(a, b), expected))
        return 1;
    }
  }
  std::puts("PASS: 256 four-state words, 65536 pairs, LRM oracle and unfurled "
            "lowering");
}
