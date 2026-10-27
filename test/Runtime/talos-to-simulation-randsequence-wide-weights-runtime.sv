// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t +ntb_random_seed=13 | FileCheck %s
// RUN: not %t +negative +ntb_random_seed=13 2>&1 | FileCheck %s --check-prefix=NEGATIVE --implicit-check-not="wide randsequence PASS"
// RUN: not %t +unknown +ntb_random_seed=13 2>&1 | FileCheck %s --check-prefix=UNKNOWN --implicit-check-not="wide randsequence PASS"

// IEEE 1800-2023 18.17.1: probabilities use complete nonnegative weights.
// Compiler-sized carry bits preserve natural sums beyond 64 and 257 bits.
// Zero/default/single-rule weights, once-only evaluation and process RNG
// preservation follow production-entry semantics. Counts are checked against
// a band of about four standard deviations.
// CHECK: wide randsequence PASS

// A negative or unknown weight is diagnosed before selection. The unknown
// weight is a 32-bit four-state scalar; a wide vector is in
// talos-to-simulation-randsequence-wide-unknown-weight-runtime.sv.
// NEGATIVE: randsequence weight evaluated to a negative value
// UNKNOWN: randsequence weight contains X or Z bits

module top;
  int first, second, zero;
  int selected = -1;
  int evaluations;
  bit [31:0] expected, actual;

  bit [63:0] w64 = '1;
  bit [129:0] wa = 130'd1 << 127;
  bit [129:0] wb = 130'd1 << 128;
  bit [256:0] w257 = '1;
  bit [256:0] wide_zero = 257'd0;
  bit signed [128:0] negative = -129'sd1;
  integer unknown = 'x;
  logic [256:0] known_logic = 257'd3;

  function automatic bit [256:0] weight();
    evaluations++;
    return 257'd3;
  endfunction

  initial begin
    if ($test$plusargs("negative")) begin
      randsequence (main)
        main : { selected = 90; } := negative | { selected = 91; } := (257'd1);
      endsequence
    end
    if ($test$plusargs("unknown")) begin
      randsequence (main)
        main : { selected = 92; } := unknown | { selected = 93; } := (257'd1);
      endsequence
    end

    // Two (2**64 - 1) weights need a 65-bit total.
    repeat (2048) begin
      randsequence (main)
        main : { first++; } := w64 | { second++; } := w64 | { zero++; } := (64'd0);
      endsequence
    end
    if (first < 915 || first > 1135)
      $fatal(1, "randsequence failed after a 64-bit weight sum overflow");
    if (first + second != 2048 || zero != 0)
      $fatal(1, "randsequence lost a valid 65-bit weight rank");

    // 130-bit weights 2**127 : 2**128 : 0 give a 1:2 split.
    first = 0; second = 0;
    repeat (4096) begin
      randsequence (main)
        main : { first++; } := wa | { second++; } := wb |
               { zero++; } := wide_zero;
      endsequence
    end
    if (first < 1210 || first > 1520)
      $fatal(1, "wide randsequence lost its 1:2 weighted distribution");
    if (first + second != 4096 || zero != 0)
      $fatal(1, "wide randsequence selected a zero weight");

    // Two (2**257 - 1) weights need a 258-bit total.
    first = 0; second = 0;
    repeat (2048) begin
      randsequence (main)
        main : { first++; } := w257 | { second++; } := w257;
      endsequence
    end
    if (first < 915 || first > 1135)
      $fatal(1, "257-bit randsequence lost its carry or partial word");
    if (first + second != 2048)
      $fatal(1, "257-bit randsequence discarded a legal production");

    // An unweighted rule has weight 1 among zero-weight rules.
    randsequence (main)
      main : { selected = 10; } := wide_zero | { selected = 11; } |
             { selected = 12; } := wide_zero;
    endsequence
    if (selected != 11)
      $fatal(1, "wide randsequence lost the default weight");

    // A four-state weight without X or Z bits is accepted.
    randsequence (main)
      main : { selected = 18; } := known_logic | { selected = 19; } := wide_zero;
    endsequence
    if (selected != 18)
      $fatal(1, "randsequence rejected a known four-state weight");

    // A zero total selects no production and draws no random number.
    process::self().srandom(93);
    expected = $urandom;
    process::self().srandom(93);
    selected = -1;
    randsequence (main)
      main : { selected = 13; } := wide_zero | { selected = 14; } := wide_zero;
    endsequence
    actual = $urandom;
    if (selected != -1 || expected != actual)
      $fatal(1, "zero-total randsequence consumed RNG or selected a production");

    // Each weight expression runs once, including the single-rule case.
    randsequence (main)
      main : { selected = 15; } := (weight()) | { selected = 16; } := wide_zero;
    endsequence
    if (selected != 15 || evaluations != 1)
      $fatal(1, "wide randsequence repeated a weight evaluation");
    randsequence (main)
      main : { selected = 17; } := (weight());
    endsequence
    if (selected != 17 || evaluations != 2)
      $fatal(1, "single-rule randsequence skipped weight evaluation");

    $display("wide randsequence PASS");
  end
endmodule
