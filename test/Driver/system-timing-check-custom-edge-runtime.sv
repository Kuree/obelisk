// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O0 --native-scheduler=aot %s -o %t.o0.aot
// RUN: %t.o0.aot | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o3.aot | FileCheck %s

`timescale 1ns / 1ps

module system_timing_check_custom_edge_runtime;
  logic [3:0] data = 0, reference = 0;
  reg notifier = 0;

  specify
    // IEEE 1800-2017 31.5 makes Z an X transition descriptor spelling and
    // 31.8 coalesces all matching bits in one vector publication.
    $setup(posedge data, edge [01, 0x, 1z, z0] reference, 3, notifier);
  endspecify

  initial begin
    data = 0;
    #1 data = '1;
    #2 reference = 4'b0100;  // A non-LSB 01 is still one vector occurrence.
    #0.001;
    $display("custom-01 %b", notifier);
    data = 0;
    #1 data = '1;
    #2 reference = 4'bxxxx;  // 0x and 1x coalesce to one occurrence.
    #0.001;
    $display("custom-x %b", notifier);
    data = 0;
    #1 data = '1;
    #2 reference = 0;  // x0.
    #0.001;
    $display("custom-x0 %b", notifier);
    data = 0;
    #1 data = '1;
    #2 reference = 4'bxxxx;  // Positive 0x coverage.
    #0.001;
    $display("custom-0x %b", notifier);
    data = 0;
    #1 data = '1;
    #2 reference = '1;  // x1 is outside the frozen subset.
    #0.001;
    $display("custom-x1 %b", notifier);
    data = 0;
    #1 data = '1;
    #2 reference = 0;  // 10 is outside the frozen subset.
    #0.001;
    $display("custom-10 %b", notifier);
    $finish;
  end
endmodule

// 0x11b is the marker plus Clause 31.5 classes 01, 0x, 1x, and x0.
// The first transition also guards full-width native and bytecode serialization.
// SIM: trigger = change
// SIM: simulation.suspend.clock_set
// SIM-SAME: edges [1, 283]
// SIM-NOT: timing_check_table
// CHECK: custom-01 1
// CHECK-NEXT: custom-x 0
// CHECK-NEXT: custom-x0 1
// CHECK-NEXT: custom-0x 0
// CHECK-NEXT: custom-x1 0
// CHECK-NEXT: custom-10 0
