// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ps

module edge_ifnone_condition(input wire clock, input wire [1:0] condition,
                             output wire q);
  assign q = clock;
  specify
    if (condition)
      (posedge clock => (q +: clock)) = 5;
    ifnone
      (clock => q) = 2;
  endspecify
endmodule

module specify_edge_ifnone_condition_runtime;
  logic clock = 0;
  logic [1:0] condition = 2'b01;
  wire q;
  edge_ifnone_condition dut(clock, condition, q);

  initial begin
    #3;

    // Clause 30.4.4.4 groups the simple ifnone with its edge-sensitive
    // conditional alternative. It must not shorten this qualified posedge.
    clock = 1;
    #2.001 $display("true-pending %b", q);
    #3 $display("true-done %b", q);

    condition = 2'b01;
    clock = 0;
    #0 $display("reset-immediate %b", q);

    // Clause 30.4.4.1 represents a vector condition by its LSB, so 2'b10 is
    // false and selects the ifnone delay.
    condition = 2'b10;
    // The input ports are copy connections. Let the condition propagate
    // before the clock edge instead of racing the two port processes.
    #0;
    clock = 1;
    #1 $display("lsb-pending %b", q);
    #1.001 $display("lsb-done %b", q);

    clock = 0;
    #2.001;

    // The same clause treats an X or Z condition result as true.
    condition = 2'bxx;
    #0;
    clock = 1;
    #2.001 $display("x-pending %b", q);
    #3 $display("x-done %b", q);
    $finish;
  end
endmodule

// CHECK: true-pending 0
// CHECK-NEXT: true-done 1
// CHECK-NEXT: reset-immediate 0
// CHECK-NEXT: lsb-pending 0
// CHECK-NEXT: lsb-done 1
// CHECK-NEXT: x-pending 0
// CHECK-NEXT: x-done 1
