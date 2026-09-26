// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/materialize.sv -o %t/materialized.mlir
// RUN: FileCheck %s --check-prefix=MATERIALIZED < %t/materialized.mlir

// Runtime behavior is checked in ../Runtime/simulation-fuse-compute-materialize.test.

// MATERIALIZED: obelisk_sim.spawn @__obelisk_fused_0
// MATERIALIZED-COUNT-1: obelisk_sim.func private @__obelisk_fused_0
// MATERIALIZED-COUNT-1: obelisk_sim.suspend.edge posedge
// MATERIALIZED-COUNT-2: obelisk_sim.nba.enqueue
// MATERIALIZED-NOT: obelisk_sim.func private @unit_0
// MATERIALIZED-NOT: obelisk_sim.func private @unit_1

//--- materialize.sv
module materialize_fusion;
  bit clock;
  int a;
  int b;

  always @(posedge clock)
    a <= a + 1;

  always @(posedge clock)
    b <= b + 2;

  initial begin
    repeat (4) begin
      #1 clock = 1;
      #1 clock = 0;
    end
    $display("%0d %0d", a, b);
    $finish;
  end
endmodule
