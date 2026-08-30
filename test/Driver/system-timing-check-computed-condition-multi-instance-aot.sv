// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --native-scheduler=aot %s -o %t.aot
// RUN: %t.aot | FileCheck %s

`timescale 1ns / 1ps

module computed_condition_cell(
    input wire data, reference, enable,
    output reg notifier = 0);
  specify
    // IEEE 1800-2017 31.7 permits a one-source expression. Each instance's
    // compiled observer must retain its own capture when a wait is reused.
    $setup(data, posedge reference &&& (enable ^ 1'b0), 3, notifier);
  endspecify
endmodule

module system_timing_check_computed_condition_multi_instance_aot;
  logic data_a = 0, reference_a = 0, enable_a = 1;
  logic data_b = 0, reference_b = 0, enable_b = 0;
  wire notifier_a, notifier_b;

  computed_condition_cell a(data_a, reference_a, enable_a, notifier_a);
  computed_condition_cell b(data_b, reference_b, enable_b, notifier_b);

  initial begin
    #1 data_a = 1;
    #2 reference_a = 1;
    #0.001 $display("computed-a %b", notifier_a);
    #1 data_b = 1;
    #2 reference_b = 1;
    #0.001 $display("computed-b-disabled %b", notifier_b);
    enable_b = 1;
    reference_b = 0;
    data_b = 0;
    #1 data_b = 1;
    #2 reference_b = 1;
    #0.001 $display("computed-b-enabled %b", notifier_b);
    $finish;
  end
endmodule

// CHECK: computed-a 1
// CHECK-NEXT: computed-b-disabled 0
// CHECK-NEXT: computed-b-enabled 1
