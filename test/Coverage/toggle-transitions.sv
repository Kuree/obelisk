module top;
  logic four_state;
  bit two_state;

  initial begin
    // Establish a known four-state baseline. X->0 is diagnostic only.
    four_state = 1'b0;
    four_state = 1'b1;
    two_state <= 1'b1;
    #1;
    four_state = 1'b0;
    two_state <= 1'b0;
    #1 $finish;
  end
endmodule

// Blocking publication (four_state) and NBA commit (two_state) both reach the
// transition recorder: each bit sees both a 0->1 and a 1->0 transition, so all
// four toggle bins are covered. The initial X->0 of the four-state variable is
// diagnostic only and is not a bin.
// RUN: %obelisk --std=1800-2023 -O0 --coverage=toggle %s -o %t
// RUN: %t --coverage-output=%t.obcov
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s

// CHECK: toggle: 100.00% (4/4)
