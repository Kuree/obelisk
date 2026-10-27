// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 11.4.3/11.4.4 and 18.3/18.5: variable divisors solve in both
// directions. A zero divisor is an invalid binding and is never drawn, the
// remaining divisors are uniformly ranked, a captured divisor is bound at the
// call, signed quotients and remainders follow truncation toward zero with
// the minimum value divided by -1 wrapping, and a failed solve (zero captured
// divisor) commits no assignment.
// CHECK: dynamic division and remainder PASS

module top;
  int ok, ones, twos, threes;
  bit [3:0] n, q, cap_d = 2, saved_n, saved_q;
  bit [2:0] d, r, saved_r;
  bit signed [4:0] sn, sq, sr;
  bit signed [3:0] sd;

  `define SDIV_CASE(N, D, Q, R) \
    ok = randomize(sn, sd, sq, sr) with { sn == N; sd == D; sq == sn / sd; sr == sn % sd; }; \
    if (!(ok == 1 && sq == Q && sr == R)) $fatal(1, "signed quotient or remainder lost its sign");

  initial begin
    repeat (384) begin
      ok = randomize(n, d, q, r) with { d < 3'd4; q == n / d; r == n % d; };
      if (ok != 1) $fatal(1, "dynamic division solve failed");
      if (d == 3'd0) $fatal(1, "dynamic division selected a zero divisor");
      if (q != n / d) $fatal(1, "dynamic division quotient differs from SV");
      if (r != n % d) $fatal(1, "dynamic division remainder differs from SV");
      if (d == 3'd1) ones++;
      if (d == 3'd2) twos++;
      if (d == 3'd3) threes++;
    end
    if (!(ones > 90 && ones < 165 && twos > 90 && twos < 165 && threes > 90 && threes < 165))
      $fatal(1, "dynamic division ranks are biased");

    ok = randomize(n, q, r) with { n == 4'd13; q == n / cap_d; r == n % cap_d; };
    if (!(ok == 1 && q == 4'd6 && r == 3'd1)) $fatal(1, "captured divisor was not bound at the call");
    saved_n = n;
    saved_q = q;
    saved_r = r;
    cap_d = 4'd0;
    ok = randomize(n, q, r) with { n == 4'd13; q == n / cap_d; r == n % cap_d; };
    if (!(ok == 0 && n == saved_n && q == saved_q && r == saved_r))
      $fatal(1, "zero-divisor failure committed assignments");

    // The most negative dividend divided by -1 wraps; the others truncate.
    ok = randomize(sn, sd, sq, sr) with { sn == -5'sd16; sd == -4'sd1; sq == sn / sd; sr == sn % sd; };
    if (!(ok == 1 && sq == -5'sd16 && sr == 5'sd0)) $fatal(1, "signed division overflow did not wrap");
    `SDIV_CASE(-5'sd13, 4'sd3, -4, -1)
    `SDIV_CASE(5'sd13, -4'sd3, -4, 1)
    `SDIV_CASE(-5'sd13, -4'sd3, 4, -1)
    $display("dynamic division and remainder PASS");
  end
endmodule
