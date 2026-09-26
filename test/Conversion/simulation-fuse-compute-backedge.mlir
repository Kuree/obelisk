// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/backedge.sv -o %t/backedge.mlir
// RUN: FileCheck %s --check-prefix=BACKEDGE-IR < %t/backedge.mlir

// Runtime behavior is checked in ../Runtime/simulation-fuse-compute-backedge.test.

// BACKEDGE-IR: obelisk_sim.design
// BACKEDGE-IR: __obelisk_fused_

//--- backedge.sv
module activating_backedge;
  bit clock;
  bit published;

  always @(posedge published)
    $display("X");

  always @(posedge clock) begin
    $display("A");
    published = 1;
  end

  always @(posedge clock)
    $display("B");

  initial
    #1 clock = 1;
endmodule
