// Generated driver: every canonical form must agree with the reference
// lowering for every pair of four-bit four-state inputs.
#include <cstdint>
#include <cstdio>

extern "C" uint64_t truth_conditions(uint8_t, uint8_t);
extern "C" uint64_t reference_truth_conditions(uint8_t, uint8_t);
extern "C" uint64_t truth_sources(uint8_t, uint8_t);
extern "C" uint64_t reference_truth_sources(uint8_t, uint8_t);
extern "C" uint64_t negations(uint8_t, uint8_t);
extern "C" uint64_t reference_negations(uint8_t, uint8_t);
extern "C" uint64_t single_bit_forms(uint8_t, uint8_t);
extern "C" uint64_t reference_single_bit_forms(uint8_t, uint8_t);
extern "C" uint64_t mux_forms(uint8_t, uint8_t);
extern "C" uint64_t reference_mux_forms(uint8_t, uint8_t);
extern "C" uint64_t bitwise_identities(uint8_t, uint8_t);
extern "C" uint64_t reference_bitwise_identities(uint8_t, uint8_t);
extern "C" uint64_t single_bit_identities(uint8_t, uint8_t);
extern "C" uint64_t reference_single_bit_identities(uint8_t, uint8_t);
extern "C" uint64_t arithmetic_identities(uint8_t, uint8_t);
extern "C" uint64_t reference_arithmetic_identities(uint8_t, uint8_t);
extern "C" uint64_t resize_forms(uint8_t, uint8_t);
extern "C" uint64_t reference_resize_forms(uint8_t, uint8_t);
extern "C" uint64_t resize_chain(uint8_t, uint8_t);
extern "C" uint64_t reference_resize_chain(uint8_t, uint8_t);
extern "C" uint64_t concat_constants(uint8_t, uint8_t);
extern "C" uint64_t reference_concat_constants(uint8_t, uint8_t);
extern "C" uint64_t extract_of_resize(uint8_t, uint8_t);
extern "C" uint64_t reference_extract_of_resize(uint8_t, uint8_t);
extern "C" uint64_t narrowing(uint8_t, uint8_t);
extern "C" uint64_t reference_narrowing(uint8_t, uint8_t);
extern "C" uint64_t extract_shift_left(uint8_t, uint8_t);
extern "C" uint64_t reference_extract_shift_left(uint8_t, uint8_t);
extern "C" uint64_t extract_shift_right(uint8_t, uint8_t);
extern "C" uint64_t reference_extract_shift_right(uint8_t, uint8_t);
extern "C" uint64_t extract_shift_right_arith(uint8_t, uint8_t);
extern "C" uint64_t reference_extract_shift_right_arith(uint8_t, uint8_t);
extern "C" uint64_t partial_selections(uint8_t, uint8_t);
extern "C" uint64_t reference_partial_selections(uint8_t, uint8_t);
extern "C" uint64_t two_state_arith(uint8_t, uint8_t);
extern "C" uint64_t reference_two_state_arith(uint8_t, uint8_t);
extern "C" uint64_t two_state_more(uint8_t, uint8_t);
extern "C" uint64_t reference_two_state_more(uint8_t, uint8_t);
extern "C" uint64_t population_counts(uint8_t, uint8_t);
extern "C" uint64_t reference_population_counts(uint8_t, uint8_t);

struct Case {
  const char *name;
  uint64_t (*canonical)(uint8_t, uint8_t);
  uint64_t (*reference)(uint8_t, uint8_t);
};

int main() {
  const Case cases[] = {
      {"truth_conditions", truth_conditions, reference_truth_conditions},
      {"truth_sources", truth_sources, reference_truth_sources},
      {"negations", negations, reference_negations},
      {"single_bit_forms", single_bit_forms, reference_single_bit_forms},
      {"mux_forms", mux_forms, reference_mux_forms},
      {"bitwise_identities", bitwise_identities, reference_bitwise_identities},
      {"single_bit_identities", single_bit_identities, reference_single_bit_identities},
      {"arithmetic_identities", arithmetic_identities, reference_arithmetic_identities},
      {"resize_forms", resize_forms, reference_resize_forms},
      {"resize_chain", resize_chain, reference_resize_chain},
      {"concat_constants", concat_constants, reference_concat_constants},
      {"extract_of_resize", extract_of_resize, reference_extract_of_resize},
      {"narrowing", narrowing, reference_narrowing},
      {"extract_shift_left", extract_shift_left, reference_extract_shift_left},
      {"extract_shift_right", extract_shift_right, reference_extract_shift_right},
      {"extract_shift_right_arith", extract_shift_right_arith, reference_extract_shift_right_arith},
      {"partial_selections", partial_selections, reference_partial_selections},
      {"two_state_arith", two_state_arith, reference_two_state_arith},
      {"two_state_more", two_state_more, reference_two_state_more},
      {"population_counts", population_counts, reference_population_counts},
  };
  unsigned failures = 0;
  for (const Case &test : cases)
    for (unsigned a = 0; a != 256; ++a)
      for (unsigned b = 0; b != 256; ++b) {
        uint64_t canonical = test.canonical(a, b);
        uint64_t reference = test.reference(a, b);
        if (canonical != reference && failures++ < 20)
          std::printf("%s a=%02x b=%02x: canonical=%016llx "
                      "reference=%016llx\n",
                      test.name, a, b, (unsigned long long)canonical,
                      (unsigned long long)reference);
      }
  if (failures) {
    std::printf("FAIL: %u mismatches\n", failures);
    return 1;
  }
  std::puts("PASS: 20 functions, 65536 four-state pairs");
}
