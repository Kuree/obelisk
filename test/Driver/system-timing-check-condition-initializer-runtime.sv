// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o3.aot | FileCheck %s

`timescale 1ns / 1ps

module system_timing_check_condition_initializer_runtime;
  logic data_a = 0, reference_a = 0, enable_a = 1;
  logic data_b = 0, reference_b = 0, enable_b = 1;
  reg notifier_a = 0, notifier_b = 0;

  specify
    $setup(data_a, posedge reference_a &&& enable_a, 3, notifier_a);
    $setup(data_b, posedge reference_b &&& enable_b, 3, notifier_b);
  endspecify

  initial begin
    #1 data_a = 1;
    #2 reference_a = 1;
    #0.001 $display("initialized-condition-a %b", notifier_a);

    #1 data_b = 1;
    #2 reference_b = 1;
    #0.001 $display("initialized-condition-b %b", notifier_b);
    $finish;
  end
endmodule

// CHECK: initialized-condition-a 1
// CHECK-NEXT: initialized-condition-b 1
