// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t +ntb_random_seed=13 | FileCheck %s

// IEEE 1800-2023 18.16: 65/129/130/257-bit weights retain unsigned
// self-determined summation, including overflow and one-time side effects.
// Non-power-of-two totals use exact rejection sampling. Zero totals preserve
// the process RNG, and a tiny dynamic total must complete promptly. Counts
// are checked against a band of about four standard deviations.
// CHECK: wide randcase PASS

module top;
  int selected = -1;
  int first, second, zero;
  int evaluations;
  bit [31:0] expected, actual;

  bit [128:0] wa = 129'd1 << 126;
  bit [128:0] wb = 129'd1 << 127;
  bit [256:0] w257 = 257'd3 << 254;
  bit [256:0] wide_zero = 257'd0;
  bit [129:0] overflow = 130'd1 << 129;
  bit [64:0] narrow_max = 65'h1ffffffffffffffff;
  bit [64:0] narrow_one = 65'd1;
  bit signed [128:0] signed_negative = -(129'sd1 <<< 128);

  function automatic bit [128:0] weight();
    evaluations++;
    return 129'd1;
  endfunction

  initial begin
    // 129-bit weights 2**126 : 2**127 : 0 give a 1:2 split.
    repeat (4096) begin
      randcase
        wa: first++;
        wb: second++;
        129'd0: zero++;
      endcase
    end
    if (first < 1210 || first > 1520)
      $fatal(1, "wide randcase lost its 1:2 weighted distribution");
    if (first + second != 4096)
      $fatal(1, "wide randcase skipped a nonzero total");
    if (zero != 0) $fatal(1, "wide randcase selected a zero weight");

    // The 257-bit total 3 * 2**255 has a final partial word.
    first = 0; second = 0;
    repeat (2048) begin
      randcase
        w257: first++;
        w257: second++;
        wide_zero: zero++;
      endcase
    end
    if (first < 915 || first > 1135)
      $fatal(1, "257-bit randcase dropped its final partial word");
    if (first + second != 2048 || zero != 0)
      $fatal(1, "257-bit randcase selected an invalid branch");

    // Only the middle weight is nonzero.
    repeat (64) begin
      randcase
        wide_zero: selected = 9;
        w257: selected = 10;
        wide_zero: selected = 11;
      endcase
      if (selected != 10)
        $fatal(1, "257-bit randcase truncated its high weight");
    end

    // A signed 129-bit weight is an unsigned summand (zero-extended), so it
    // equals the 257-bit weight 2**128 and the two branches split evenly.
    first = 0; second = 0;
    repeat (1024) begin
      randcase
        signed_negative: first++;
        257'd1 << 128: second++;
      endcase
    end
    if (first < 412 || first > 612 || first + second != 1024)
      $fatal(1, "wide randcase sign-extended or discarded a weight");

    // The 65-bit self-determined sum wraps to zero before it is widened.
    repeat (64) begin
      randcase
        narrow_max + narrow_one: selected = 14;
        130'd1: selected = 15;
      endcase
      if (selected != 15)
        $fatal(1, "wide randcase widened a self-determined expression");
    end

    // Two 2**129 weights sum to zero modulo 2**130: no branch, no RNG draw.
    process::self().srandom(93);
    expected = $urandom;
    process::self().srandom(93);
    selected = -1;
    randcase
      overflow: selected = 16;
      overflow: selected = 17;
    endcase
    actual = $urandom;
    if (selected != -1 || actual != expected)
      $fatal(1, "wide zero-total randcase consumed RNG or selected a branch");

    // A weight expression runs once and a total of one terminates.
    randcase
      weight(): selected = 18;
      wide_zero: selected = 19;
    endcase
    if (selected != 18 || evaluations != 1)
      $fatal(1, "wide randcase repeated its weight evaluation");

    $display("wide randcase PASS");
  end
endmodule
