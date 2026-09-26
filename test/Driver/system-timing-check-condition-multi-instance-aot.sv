// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o3.aot | FileCheck %s

`timescale 1ns / 1ps

module timing_check_condition_cell(
    input wire data, reference, enable,
    output reg notifier = 0);
  specify
    $setup(data, posedge reference &&& enable, 3, notifier);
  endspecify
endmodule

module system_timing_check_condition_multi_instance_aot;
  logic data_a = 0, reference_a = 0, enable_a = 1;
  logic data_b = 0, reference_b = 0, enable_b = 1;
  wire notifier_a, notifier_b;

  timing_check_condition_cell a(
      data_a, reference_a, enable_a, notifier_a);
  timing_check_condition_cell b(
      data_b, reference_b, enable_b, notifier_b);

  initial begin
    #1 data_a = 1;
    #2 reference_a = 1;
    #0.001 $display("aot-condition-a %b", notifier_a);
    #1 data_b = 1;
    #2 reference_b = 1;
    #0.001 $display("aot-condition-b %b", notifier_b);
    $finish;
  end
endmodule

// CHECK: aot-condition-a 1
// CHECK-NEXT: aot-condition-b 1
