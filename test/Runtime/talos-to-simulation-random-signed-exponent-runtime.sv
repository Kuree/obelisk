// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 11.4.3 Table 11-4 and 18.3: the signed, self-determined
// exponent of ** keeps parity for a base of -1, truncates the reciprocal for
// other integral bases, yields one for a zero exponent, and a zero base with
// a negative exponent (undefined) is never sampled.
// CHECK: dynamic signed exponent PASS

module top;
  int ok, negatives, zero_exp;
  bit signed [3:0] base, exp;
  bit signed [7:0] value;

  // Literal operands, so the relation is a compile-time constraint.
  `define POW_CASE(B, E, WANT) \
    ok = randomize(base, exp, value) with { base == B; exp == E; value == base ** exp; }; \
    if (!(ok == 1 && value == WANT)) $fatal(1, "integral power Table 11-4 case failed");

  initial begin
    repeat (256) begin
      ok = randomize(base, exp, value) with {
        base >= -2; base <= 2; exp >= -3; exp <= 3; value == base ** exp;
      };
      if (ok != 1) $fatal(1, "signed exponent solve failed");
      if (value != base ** exp) $fatal(1, "signed exponent disagrees with SV");
      if (!(base != 0 || exp >= 0)) $fatal(1, "zero raised to a negative power was sampled");
      if (exp < 0) negatives++;
      if (exp == 0) zero_exp++;
    end
    if (!(negatives > 65 && negatives < 130 && zero_exp > 20 && zero_exp < 65))
      $fatal(1, "signed power ranks are biased");
    `POW_CASE(-1, -3, -1)
    `POW_CASE(-1, -2, 1)
    `POW_CASE(-2, -3, 0)
    `POW_CASE(1, -8, 1)
    `POW_CASE(2, -1, 0)
    `POW_CASE(0, 0, 1)
    `POW_CASE(-2, 3, -8)
    $display("dynamic signed exponent PASS");
  end
endmodule
