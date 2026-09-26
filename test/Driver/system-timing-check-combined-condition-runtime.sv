// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
`timescale 1ns / 1ps

module system_timing_check_combined_condition_runtime;
  logic reference = 0, data = 0;
  logic reference_condition = 1, data_condition = 0;
  reg notifier = 0;

  specify
    $setuphold(posedge reference &&& reference_condition,
               posedge data &&& data_condition, 3, 3, notifier);
  endspecify

  initial begin
    // Clause 31.7 filters each combined event at its own publication. The
    // disabled data event must not seed the setup timestamp.
    #1 data = 1;
    #2 reference = 1;
    #0.001 $display("combined-data-disabled %b", notifier);

    #5 data = 0;
    reference = 0;
    data_condition = 1;
    reference_condition = 0;
    #1 data = 1;
    #2 reference = 1;
    #0.001 $display("combined-reference-disabled %b", notifier);

    #5 data = 0;
    reference = 0;
    reference_condition = 1;
    #1 data = 1;
    #2 reference = 1;
    #0.001 $display("combined-both-enabled %b", notifier);
    $finish;
  end
endmodule

// SIM: obelisk_sim.suspend.clock_set
// SIM-SAME: conditions 2 edges [1, 1] indices [0, 1]
// SIM-NOT: timing_check_table
// CHECK: combined-data-disabled 0
// CHECK-NEXT: combined-reference-disabled 0
// CHECK-NEXT: combined-both-enabled 1
