// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ns

module procedural_edge_twelve(
    input wire clock, reset, input logic data, output logic destination);
  always @(posedge clock)
    destination = data;
  always @(posedge reset)
    destination = data;
  specify
    (posedge clock => (destination +: data)) =
        (8, 8, 8, 8, 8, 8, 2, 8, 8, 8, 4, 3);
  endspecify
endmodule

module specify_edge_procedural_twelve_delays;
  logic clock, reset, data;
  logic destination;
  procedural_edge_twelve dut(clock, reset, data, destination);
  initial begin
    clock = 0; reset = 0; data = 0;
    #1 reset = 1; #1 reset = 0;

    data = 1'bx;
    #1 clock = 1; #1 clock = 0;
    $display("0x-pending %0t %b", $time, destination);
    #1 $display("0x-done %0t %b", $time, destination);

    data = 1'bz;
    #1 clock = 1; #1 clock = 0;
    $display("xz-pending %0t %b", $time, destination);
    #3 $display("xz-done %0t %b", $time, destination);

    data = 1'bx;
    #1 clock = 1; #1 clock = 0;
    $display("zx-pending %0t %b", $time, destination);
    #2 $display("zx-done %0t %b", $time, destination);
    $finish;
  end
endmodule

// CHECK: 0x-pending 4 0
// CHECK-NEXT: 0x-done 5 x
// CHECK-NEXT: xz-pending 7 x
// CHECK-NEXT: xz-done 10 z
// CHECK-NEXT: zx-pending 12 z
// CHECK-NEXT: zx-done 14 x
