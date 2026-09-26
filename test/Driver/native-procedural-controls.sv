// RUN: obelisk %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

`timescale 1ns/1ps
module native_procedural_controls;
  logic clk;
  logic enable;
  logic lhs;
  logic rhs;
  int iterations;

  initial begin
    clk = 0;
    enable = 0;
    rhs = 1;
    #1 enable = 1;
    #1 clk = 1;
    #1 clk = 0;
    #1 clk = 1;
    #1 clk = 0;
    #1 clk = 1;
    #1 rhs = 0;
  end

  initial begin
    repeat (3) begin
      iterations++;
      continue;
      iterations = iterations + 100;
    end
    $display("continue=%0d", iterations);
    #1step;
    $display("one-step");
    wait (enable)
      $display("wait=%0d", enable);
    repeat (2) @(posedge clk);
    $display("repeat=%0d", clk);
    @(posedge clk iff enable);
    $display("iff=%0d", enable);
    @*
      lhs = rhs;
    $display("star=%0d", lhs);
  end
endmodule

// CHECK: continue=3
// CHECK-NEXT: one-step
// CHECK-NEXT: wait=1
// CHECK-NEXT: repeat=1
// CHECK-NEXT: iff=1
// CHECK-NEXT: star=0
