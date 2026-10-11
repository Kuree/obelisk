// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 11.3.4, 11.4.7, 11.4.8, 18.3 and 18.5.12/18.5.13:
// division by zero is an unknown value, not an evaluation error, and the
// compiler's sampling plans carry a value plane and an unknown plane.
// Checked here:
//  - inactive if/implication guards never evaluate the zero division, an
//    active guard whose body is undefined fails atomically, an arithmetic
//    guard keeps its hard constraints, and a zero divisor in a guard condition
//    is an invalid binding;
//  - a discarded soft constraint neither changes the hard values nor admits an
//    undefined binding, and the remaining divisors stay uniformly ranked;
//  - two-state casts, known OR/AND bits, bitwise not, reductions and
//    $countones of undefined arithmetic, for a constrained divisor and for a
//    live capture;
//  - four-state captures coerced bitwise by a cast and rejected without one,
//    and unknown sign bits through >>> and a widening cast.
// CHECK: arithmetic guards and casts PASS

module top;
  typedef bit [3:0] b4_t;
  typedef logic [3:0] l4_t;
  typedef logic signed [3:0] ls4_t;
  typedef logic [7:0] l8_t;

  int ok, ones, twos, threes;
  bit [3:0] n, d, q, cap_d, saved_q;
  bit gate, cap_gate;
  logic [3:0] cap_bits = 4'b10xz;
  logic signed [3:0] cap_signed = 4'bx010;

  // d == 0 constrained in the call, and cap_d (zero) captured by the call.
  `define UNDEF_D(EXPR, WANT, MSG) \
    ok = randomize(n, q, d) with { n == 11; q == (EXPR); d == 0; }; \
    if (!(ok == 1 && q == WANT)) $fatal(1, MSG);
  `define UNDEF_CAP(EXPR, WANT, MSG) \
    ok = randomize(n, q) with { n == 11; q == (EXPR); }; \
    if (!(ok == 1 && q == WANT)) $fatal(1, MSG);

  // The same relation twice: [D] reads the constrained d, [CAP] the capture.
  `define UNDEF_BOTH(EXPR_D, EXPR_CAP, WANT, MSG) \
    `UNDEF_D(EXPR_D, WANT, MSG) \
    `UNDEF_CAP(EXPR_CAP, WANT, MSG)

  initial begin
    ok = randomize(n, q) with { n == 11; q == 7; if (cap_gate) q == n / cap_d; };
    if (!(ok == 1 && q == 7)) $fatal(1, "inactive state guard evaluated zero division");
    cap_gate = 1;
    saved_q = q;
    ok = randomize(n, q) with { n == 11; q == 7; if (cap_gate) q == n / cap_d; };
    if (!(ok == 0 && q == saved_q)) $fatal(1, "active undefined guard body changed state");
    cap_gate = 0;
    ok = randomize(n, q) with { n == 11; q == 7; (cap_d != 0) -> q == n / cap_d; };
    if (!(ok == 1 && q == 7)) $fatal(1, "inactive implication evaluated zero division");

    ok = randomize(n, d, q) with { n == 11; d == 2; q == 7; if ((n / d) > 0) soft q == 0; };
    if (!(ok == 1 && q == 7)) $fatal(1, "arithmetic guard did not retain hard constraints");
    ok = randomize(n, d, q) with { n == 11; d == 0; q == 7; if ((n / d) > 0) soft q == 0; };
    if (ok != 0) $fatal(1, "soft-only arithmetic guard accepted an undefined binding");
    ok = randomize(n, d, q) with { n == 11; d == 2; q == 7; ((n / d) > 0) -> { soft q == 0; } };
    if (!(ok == 1 && q == 7)) $fatal(1, "arithmetic guard did not retain hard constraints");
    ok = randomize(n, d, q) with { n == 11; d == 0; q == 7; ((n / d) > 0) -> { soft q == 0; } };
    if (ok != 0) $fatal(1, "soft-only arithmetic guard accepted an undefined binding");

    repeat (144) begin
      ok = randomize(n, d, q) with { n == 11; q == 0; d < 4; soft q == (n / d); };
      if (!(ok == 1 && q == 0)) $fatal(1, "discarded arithmetic soft constraint affected hard values");
      if (d == 0) $fatal(1, "discarded soft constraint admitted undefined arithmetic");
      if (d == 1) ones++;
      if (d == 2) twos++;
      if (d == 3) threes++;
    end
    if (!(ones > 24 && twos > 24 && threes > 24 && ones < 74 && twos < 74 && threes < 74))
      $fatal(1, "soft arithmetic ranks are biased");

    `UNDEF_BOTH(b4_t'((n / d) + 4'd3), b4_t'((n / cap_d) + 4'd3), 0, "two-state cast failed to coerce the entire undefined sum")
    `UNDEF_BOTH(b4_t'((n / d) | 4'd8), b4_t'((n / cap_d) | 4'd8), 8, "two-state cast lost known OR bits")
    `UNDEF_BOTH(b4_t'((n / d) & 4'd8), b4_t'((n / cap_d) & 4'd8), 0, "two-state cast invented AND bits")
    `UNDEF_BOTH(b4_t'(~((n / d) | 4'd8)), b4_t'(~((n / cap_d) | 4'd8)), 0, "two-state cast inverted unknown bits into ones")
    ok = randomize(n, q, d) with { n == 11; q == 4'd15; d == 0; q == ((n / d) | 4'd15); };
    if (!(ok == 1 && q == 15)) $fatal(1, "known bitwise operand failed to absorb unknown arithmetic");
    ok = randomize(n, q) with { n == 11; q == 4'd15; q == ((n / cap_d) | 4'd15); };
    if (!(ok == 1 && q == 15)) $fatal(1, "known bitwise operand failed to absorb unknown arithmetic");
    `UNDEF_BOTH(b4_t'(|((n / d) | 4'd8)), b4_t'(|((n / cap_d) | 4'd8)), 1, "reduction OR ignored a known one")
    `UNDEF_BOTH(b4_t'(&((n / d) & 4'd7)), b4_t'(&((n / cap_d) & 4'd7)), 0, "reduction AND ignored a known zero")
    `UNDEF_BOTH(b4_t'($countones((n / d) | 4'd8)), b4_t'($countones((n / cap_d) | 4'd8)), 1, "countones counted unknown arithmetic bits")

    ok = randomize(q) with { q == b4_t'(cap_bits); };
    if (!(ok == 1 && q == 8)) $fatal(1, "four-state capture was not coerced bitwise");
    ok = randomize(q) with { q == b4_t'(cap_bits | 4'd4); };
    if (!(ok == 1 && q == 12)) $fatal(1, "four-state capture lost known masked bits");
    saved_q = q;
    ok = randomize(q) with { q == cap_bits; };
    if (!(ok == 0 && q == saved_q)) $fatal(1, "uncoerced X/Z capture was accepted");
    saved_q = q;
    ok = randomize(q) with { q == ls4_t'((cap_signed >>> 3) & 4'sd8); };
    if (!(ok == 0 && q == saved_q)) $fatal(1, "arithmetic right shift lost an unknown sign bit");
    ok = randomize(q) with { q == b4_t'(cap_signed >>> 3); };
    if (!(ok == 1 && q == 0)) $fatal(1, "arithmetic right shift did not coerce unknown sign bits");
    saved_q = q;
    ok = randomize(q) with { q == l4_t'((l8_t'(cap_signed) & 8'hf0) >> 4); };
    if (!(ok == 0 && q == saved_q)) $fatal(1, "widening cast lost an unknown sign bit");
    saved_q = q;
    ok = randomize(q) with { q == b4_t'((l8_t'(cap_signed) & 8'hf0) >> 4); };
    if (!(ok == 1 && q == saved_q)) $fatal(1, "two-state widening cast did not consume unknown bits");
    $display("arithmetic guards and casts PASS");
  end
endmodule
