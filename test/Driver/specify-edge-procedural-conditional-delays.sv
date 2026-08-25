// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ns

module conditional_procedural_edge(
    input wire clock, reset, enable, data, output logic destination);
  always @(posedge clock)
    destination = data;
  always @(posedge reset)
    destination = data;
  specify
    if (enable)
      (posedge clock => (destination +: data)) = (2, 3, 4, 5, 6, 7);
  endspecify
endmodule

module specify_edge_procedural_conditional_delays;
  logic clock, reset, enable, data;
  logic destination;
  conditional_procedural_edge dut(clock, reset, enable, data, destination);
  initial begin
    clock = 0; reset = 0; enable = 0; data = 0;
    #1 reset = 1; #1 reset = 0;
    data = 1;
    #1 clock = 1; #1 clock = 0;
    $display("condition-false %0t %b", $time, destination);

    enable = 1; data = 0;
    #1 clock = 1; #1 clock = 0;
    $display("fall-pending %0t %b", $time, destination);
    #2 $display("fall-done %0t %b", $time, destination);

    data = 1;
    #1 clock = 1; #1 clock = 0;
    $display("rise-pending %0t %b", $time, destination);
    #1 $display("rise-done %0t %b", $time, destination);

    data = 1'bx;
    #1 clock = 1; #1 clock = 0;
    $display("x-pending %0t %b", $time, destination);
    #2 $display("x-done %0t %b", $time, destination);
    $finish;
  end
endmodule

// CHECK: condition-false 4 1
// CHECK-NEXT: fall-pending 6 1
// CHECK-NEXT: fall-done 8 0
// CHECK-NEXT: rise-pending 10 0
// CHECK-NEXT: rise-done 11 1
// CHECK-NEXT: x-pending 13 1
// CHECK-NEXT: x-done 15 x
