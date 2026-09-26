// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/order.sv -o %t/order.mlir
// RUN: FileCheck %s --check-prefix=COHORT < %t/order.mlir

// Runtime behavior is checked in ../Runtime/simulation-fuse-compute-order.test.

// COHORT: __obelisk_fused_

//--- order.sv
module activation_order;
  bit c;
  bit d;
  bit trigger;
  int x;
  int sampled;

  always @(posedge c)
    x = 1;

  always @(posedge d)
    sampled = x;

  always @(posedge c)
    x = 2;

  initial begin
    #1 trigger = 1;
    c = trigger;
    d = trigger;
    #1;
    $display("%0d", sampled);
    $finish;
  end
endmodule
