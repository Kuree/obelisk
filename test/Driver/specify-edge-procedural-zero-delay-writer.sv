// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ns

module procedural_edge_zero_delay(
    input wire clock, reset, enable, data, output logic destination);
  always @(posedge clock)
    #0 destination = data;
  always @(posedge reset)
    destination = data;
  specify
    if (enable)
      (posedge clock => (destination +: data)) = 2;
  endspecify
endmodule

module specify_edge_procedural_zero_delay_writer;
  logic clock, reset, enable, data;
  logic destination;
  procedural_edge_zero_delay dut(clock, reset, enable, data, destination);
  initial begin
    clock = 0; reset = 0; enable = 0; data = 0;
    #1 reset = 1; #1 reset = 0;

    enable = 1; data = 1;
    #1 clock = 1;
    // The destination actor samples the true condition at its direct wake,
    // before suspending at #0. Disable it in the same-time inactive region.
    #0 enable = 0;
    #0 $display("qualified-pending %0t %b", $time, destination);
    #2 $display("qualified-done %0t %b", $time, destination);

    clock = 0; data = 0;
    #1 clock = 1;
    #0;
    #0 $display("condition-false %0t %b", $time, destination);
    $finish;
  end
endmodule

// CHECK: qualified-pending 3 0
// CHECK-NEXT: qualified-done 5 1
// CHECK-NEXT: condition-false 6 0
