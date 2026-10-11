// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.3 and 18.12: scope randomize over a signed 129-bit and an
// unsigned 257-bit enum draws only enumerators, keeps the live capture
// constraints, ranks the signed enumerators by value rather than by raw bits,
// and a failed solve leaves both variables unchanged.
// CHECK: wide scope enum domains PASS

module top;
  typedef enum bit signed [128:0] {
    S_MIN = 129'sh100000000000000000000000000000000,
    S_M1  = 129'sh1ffffffffffffffffffffffffffffffff,
    S_P64 = 129'sh10000000000000000,
    S_MAX = 129'sh0ffffffffffffffffffffffffffffffff
  } signed_e;
  typedef enum bit [256:0] {
    W_0    = 257'h0,
    W_2_128 = 257'h100000000000000000000000000000000,
    W_2_256 = 257'h10000000000000000000000000000000000000000000000000000000000000000,
    W_2_256_9 = 257'h10000000000000000000000000000000000000000000000000000000000000009
  } wide_e;

  signed_e signed_value, saved_signed;
  wide_e wide_value, saved_wide;
  bit signed [128:0] limit = 129'sh1ffffffffffffffffffffffffffffffff;
  bit [256:0] wanted;
  int ok, negative;

  initial begin
    repeat (128) begin
      ok = randomize(signed_value, wide_value) with { signed_value <= limit; wide_value == wanted; };
      if (ok != 1) $fatal(1, "wide scope enum solve failed");
      if (!(signed_value == S_MIN || signed_value == S_M1)) $fatal(1, "signed enum comparison lost its sign");
      if (wide_value != wanted) $fatal(1, "wide scope enum lost its capture");
      if (signed_value == S_MIN) negative++;
    end
    if (!(negative > 40 && negative < 88)) $fatal(1, "signed enum ranks are biased");

    wanted = 257'h100000000000000000000000000000000;
    ok = randomize(signed_value, wide_value) with { signed_value <= limit; wide_value == wanted; };
    if (!(ok == 1 && wide_value == wanted)) $fatal(1, "257-bit enumerator was truncated");
    wanted = 257'h10000000000000000000000000000000000000000000000000000000000000000;
    ok = randomize(signed_value, wide_value) with { signed_value <= limit; wide_value == wanted; };
    if (!(ok == 1 && wide_value == wanted)) $fatal(1, "257-bit enumerator was truncated");
    wanted = 257'h10000000000000000000000000000000000000000000000000000000000000009;
    ok = randomize(signed_value, wide_value) with { signed_value <= limit; wide_value == wanted; };
    if (!(ok == 1 && wide_value == wanted)) $fatal(1, "257-bit enumerator was truncated");

    saved_signed = signed_value;
    saved_wide = wide_value;
    wanted = 257'd7;
    ok = randomize(signed_value, wide_value) with { signed_value <= limit; wide_value == wanted; };
    if (ok != 0) $fatal(1, "wide enum accepted a non-enumerator");
    if (!(signed_value == saved_signed && wide_value == saved_wide)) $fatal(1, "failed wide enum solve changed state");
    $display("wide scope enum domains PASS");
  end
endmodule
