// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/deadline-order.sv -o %t/deadline-order.mlir
// RUN: FileCheck %s --check-prefix=COHORT < %t/deadline-order.mlir

// Runtime behavior is checked in ../Runtime/simulation-fuse-compute-deadline-order.test.

// COHORT: __obelisk_fused_

//--- deadline-order.sv
module deadline_activation_order;
  bit clock;
  int value;
  int sampled;

  always @(posedge clock)
    value = 1;

  initial
    #1 clock = 1;

  initial begin
    #1 sampled = value;
    #1;
    $display("%0d", sampled);
    $finish;
  end

  always @(posedge clock)
    value = 2;
endmodule
