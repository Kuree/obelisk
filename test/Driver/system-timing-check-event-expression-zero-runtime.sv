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

module system_timing_check_event_expression_zero_runtime;
  logic data = 0, reference = 0;
  logic [3:0] condition = 0;
  reg eq_notifier = 0, ne_notifier = 0;
  reg case_eq_notifier = 0, case_ne_notifier = 0;

  specify
    // IEEE 1800-2017 31.7: ==/!= are nondeterministic, ===/!== are
    // deterministic, and a multibit conditioning expression contributes only
    // its LSB. These are compact encodings 2, 4, 6, and 8 respectively.
    $setup(posedge data, posedge reference &&& (condition == 0), 3,
           eq_notifier);
    $setup(posedge data, posedge reference &&& (condition != 0), 3,
           ne_notifier);
    $setup(posedge data, posedge reference &&& (condition === 0), 3,
           case_eq_notifier);
    $setup(posedge data, posedge reference &&& (condition !== 0), 3,
           case_ne_notifier);
  endspecify

  initial begin
    condition = 4'b0000;
    data = 0; reference = 0;
    #1 data = 1; #2 reference = 1; #0.001;
    $display("zero %b %b %b %b", eq_notifier, ne_notifier,
             case_eq_notifier, case_ne_notifier);

    condition = 4'b0001;
    data = 0; reference = 0;
    #1 data = 1; #2 reference = 1; #0.001;
    $display("one %b %b %b %b", eq_notifier, ne_notifier,
             case_eq_notifier, case_ne_notifier);

    condition = 4'bxxxx;
    data = 0; reference = 0;
    #1 data = 1; #2 reference = 1; #0.001;
    $display("x %b %b %b %b", eq_notifier, ne_notifier,
             case_eq_notifier, case_ne_notifier);

    condition = 4'bzzzz;
    data = 0; reference = 0;
    #1 data = 1; #2 reference = 1; #0.001;
    $display("z %b %b %b %b", eq_notifier, ne_notifier,
             case_eq_notifier, case_ne_notifier);

    condition = 4'b0010; // Nonzero vector, but Clause 31.7 samples its LSB.
    data = 0; reference = 0;
    #1 data = 1; #2 reference = 1; #0.001;
    $display("multibit %b %b %b %b", eq_notifier, ne_notifier,
             case_eq_notifier, case_ne_notifier);
    $finish;
  end
endmodule

// SIM: condition_predicates = array<i32: 2>
// SIM: condition_predicates = array<i32: 4>
// SIM: condition_predicates = array<i32: 6>
// SIM: condition_predicates = array<i32: 8>
// SIM-NOT: timing_check_table
// CHECK: zero 1 0 1 0
// CHECK-NEXT: one 1 1 1 1
// CHECK-NEXT: x 0 0 1 1
// CHECK-NEXT: z 1 1 1 1
// CHECK-NEXT: multibit 0 1 0 1
