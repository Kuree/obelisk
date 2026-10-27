// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: not %t +ntb_random_seed=13 2>&1 | FileCheck %s --implicit-check-not="unknown weight selected"

// A four-state weight vector with X or Z bits is diagnosed like a four-state
// scalar weight (IEEE 1800-2023 18.17.1 requires a nonnegative integral
// value) rather than used as if its unknown bits were zero.
// CHECK: randsequence weight contains X or Z bits

module top;
  logic [256:0] unknown = 257'bx;
  initial begin
    randsequence (main)
      main : { $display("unknown weight selected"); } := unknown |
             { $display("unknown weight selected"); } := (257'd1);
    endsequence
  end
endmodule
