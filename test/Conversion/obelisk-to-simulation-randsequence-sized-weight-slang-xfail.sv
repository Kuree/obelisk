// XFAIL: *
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s -o %t.mlir

// IEEE 1800-2023 A.6.12 defines rs_weight_specification as integral_number,
// ps_identifier or a parenthesized expression; a sized literal such as 8'd1
// is an integral_number (A.8.7). Pristine Slang v11.0 reports "expected ';'".
module top;
  int a;
  initial
    randsequence (main)
      main : { a = 1; } := 8'd1 | { a = 2; } := 3;
    endsequence
endmodule
