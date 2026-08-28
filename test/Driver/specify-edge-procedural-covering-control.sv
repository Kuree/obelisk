// RUN: obelisk -emit-sim %s -o - | FileCheck %s
// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s --check-prefix=RUNTIME

`timescale 1ns / 1ps

module covering_control(input wire clock, data, output logic q);
  // IEEE 1800-2017 9.4.2: an edge-free event control wakes on every value
  // change, so it covers the narrower Clause 30.4.3 posedge source event.
  always @(clock)
    q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule

module specify_edge_procedural_covering_control;
  logic clock = 0;
  logic data = 0;
  logic q;
  covering_control dut(clock, data, q);

  initial begin
    #1 data = 1;
    #1;
    clock = 1;
    #1 $display("posedge-pending %b", q);
    #1.001 $display("posedge-done %b", q);

    // The covering change control also wakes on negedge, but the narrower
    // Clause 30 path does not qualify and must add no delay.
    data = 0;
    #1;
    clock = 0;
    #0 $display("negedge-immediate %b", q);
    $finish;
  end
endmodule

// CHECK: procedural_wake_kind = 2 : i32
// CHECK-NOT: obelisk_sim.timing_path_monitor_rules
// RUNTIME: posedge-pending 0
// RUNTIME-NEXT: posedge-done 1
// RUNTIME-NEXT: negedge-immediate 0
