// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
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

module system_timing_check_condition_vector_runtime;
  logic [3:0] setup_data = 0, setup_reference = 0, setup_condition = 0;
  logic [3:0] hold_reference = 0, hold_data = '1, hold_condition = 1;
  reg setup_notifier = 0, hold_notifier = 0;

  specify
    $setup(setup_data,
           edge [01, 0x, x1, 10, 1x, x0] setup_reference &&& setup_condition,
           3, setup_notifier);
    // Z spellings are Clause 31.5 aliases for X and canonicalize to the
    // existing posedge/negedge clock subscription masks.
    $hold(edge [01, 0z, z1] hold_reference,
          edge [10, 1Z, Z0] hold_data &&& hold_condition,
          3, hold_notifier);
  endspecify

  initial begin
    // Clause 31.7 uses only the LSB; an upper known one does not enable.
    setup_condition = 4'b0010;
    #1 setup_data = 4'hf;
    #2 setup_reference = 4'hf;
    #0.001 $display("condition-upper-only %b", setup_notifier);

    setup_condition = 4'bxxxx;
    setup_data = 0;
    setup_reference = 0;
    #1 setup_data = 4'hf;
    #2 setup_reference = 4'hf;
    #0.001 $display("condition-x %b", setup_notifier);

    setup_condition = 4'bzzzz;
    setup_data = 0;
    setup_reference = 0;
    #1 setup_data = 4'hf;
    #2 setup_reference = 4'hf;
    #0.001 $display("condition-z %b", setup_notifier);

    setup_condition = 1;
    setup_data = 0;
    setup_reference = 0;
    #1 setup_data = 4'hf;
    #2 setup_reference = 4'hf;
    // Four data changes and four reference edges form one Clause 31.8 check,
    // so the notifier toggles once rather than four times.
    #0.001 $display("condition-known-one-vector-once %b", setup_notifier);

    #1 hold_reference = 4'hf;
    #2 hold_data = 0;
    #0.001 $display("canonical-z-descriptors-vector-once %b", hold_notifier);

    $finish;
  end
endmodule

// SIM: simulation.ref.extract
// SIM: simulation.suspend.clock_set
// SIM-SAME: conditions 1
// SIM-NOT: timing_check_table
// CHECK: condition-upper-only 0
// CHECK-NEXT: condition-x 0
// CHECK-NEXT: condition-z 0
// CHECK-NEXT: condition-known-one-vector-once 1
// CHECK-NEXT: canonical-z-descriptors-vector-once 1
