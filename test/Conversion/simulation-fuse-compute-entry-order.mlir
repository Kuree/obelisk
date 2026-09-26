// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/entry-order.sv -o %t/entry-order.mlir
// RUN: FileCheck %s --check-prefix=ENTRY-FUSED < %t/entry-order.mlir

// Runtime behavior is checked in ../Runtime/simulation-fuse-compute-entry-order.test.

// ENTRY-FUSED: __obelisk_fused_

//--- entry-order.sv
module initial_entry_order;
  bit clock;
  int first;
  int second;

  always @(posedge clock)
    first = 1;

  initial
    clock = 1;

  always @(posedge clock)
    second = 1;

  initial begin
    #1;
    $display("%0d %0d", first, second);
    $finish;
  end
endmodule
