// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

`timescale 1ns/1ps
module simulation_procedural_controls;
  logic clk;
  logic enable;
  logic lhs;
  logic rhs;
  int count;

  initial begin
    #1step;
    wait (enable)
      lhs = rhs;
    repeat (count) @(posedge clk)
      lhs = rhs;
    repeat (3) begin
      count++;
      continue;
      count = count + 100;
    end
    @(posedge clk iff enable)
      lhs = rhs;
    @*
      lhs = rhs;
  end
endmodule

// #1step is one design-precision tick.
// CHECK: obelisk_sim.time.constant 1
// CHECK: obelisk_sim.suspend.delay

// `wait` first tests its condition and uses a level-sensitive suspension when
// the initial test is false.
// CHECK: obelisk_sim.logic.is_true
// CHECK: cf.cond_br
// CHECK: obelisk_sim.suspend.level

// A dynamic repeated event carries its encounter-time count across resumes.
// CHECK: arith.cmpi sgt
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: arith.subi
// A repeat-loop continue verifies with the current count passed to the
// decrement block.
// CHECK: cf.br ^{{.*}}(%{{.*}} : i64)

// `iff` is sampled atomically with the primary event occurrence.
// CHECK: obelisk_sim.suspend.edge_iff posedge

// @* derives sensitivity from reads in its controlled statement.
// CHECK: obelisk_sim.suspend.change
