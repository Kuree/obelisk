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

module system_timing_check_event_expression_runtime;
  logic data = 0, reference = 0;
  logic [3:0] condition = 0;
  reg not_notifier = 0, eq_notifier = 0, ne_notifier = 0;
  reg case_eq_notifier = 0, case_ne_notifier = 0;
  reg computed_eq_notifier = 0, computed_ne_notifier = 0;
  reg computed_case_eq_notifier = 0, computed_case_ne_notifier = 0;

  specify
    // IEEE 1800-2017 31.7 samples each predicate at the controlled event.
    // X enables nondeterministic ==/!=, but disables bare/~ and ===/!==.
    $setup(posedge data, posedge reference &&& (~condition), 3,
           not_notifier);
    $setup(posedge data, posedge reference &&& (condition == 1), 3,
           eq_notifier);
    $setup(posedge data, posedge reference &&& (condition != 1), 3,
           ne_notifier);
    $setup(posedge data, posedge reference &&& (condition === 1), 3,
           case_eq_notifier);
    $setup(posedge data, posedge reference &&& (condition !== 1), 3,
           case_ne_notifier);
    // The inner expression is deliberately nontrivial so it is compiled as
    // an observer. IEEE 1800-2017 31.7 says X enables both nondeterministic
    // ==/!= forms and disables both deterministic ===/!== forms; preserve the
    // observer's four-state LSB until that frozen predicate is applied.
    $setup(posedge data, posedge reference &&&
           ((condition & 4'bxxxx) == 0), 3, computed_eq_notifier);
    $setup(posedge data, posedge reference &&&
           ((condition & 4'bxxxx) != 0), 3, computed_ne_notifier);
    $setup(posedge data, posedge reference &&&
           ((condition & 4'bxxxx) === 0), 3, computed_case_eq_notifier);
    $setup(posedge data, posedge reference &&&
           ((condition & 4'bxxxx) !== 0), 3, computed_case_ne_notifier);
  endspecify

  initial begin
    condition = 0;
    data = 0;
    reference = 0;
    #1 data = 1;
    #2 reference = 1;
    #0.001;
    $display("condition-zero %b %b %b %b %b", not_notifier, eq_notifier,
             ne_notifier, case_eq_notifier, case_ne_notifier);
    computed_eq_notifier = 0;
    computed_ne_notifier = 0;
    computed_case_eq_notifier = 0;
    computed_case_ne_notifier = 0;
    condition = 4'bxxxx;
    data = 0;
    reference = 0;
    #1 data = 1;
    #2 reference = 1;
    #0.001;
    $display("condition-x %b %b %b %b %b", not_notifier, eq_notifier,
             ne_notifier, case_eq_notifier, case_ne_notifier);
    $display("computed-x %b %b %b %b", computed_eq_notifier,
             computed_ne_notifier, computed_case_eq_notifier,
             computed_case_ne_notifier);
    condition = 1;
    data = 0;
    reference = 0;
    #1 data = 1;
    #2 reference = 1;
    #0.001;
    $display("condition-one %b %b %b %b %b", not_notifier, eq_notifier,
             ne_notifier, case_eq_notifier, case_ne_notifier);
    $finish;
  end
endmodule

// SIM: condition_predicates = array<i32: 1>
// SIM: condition_predicates = array<i32: 3>
// SIM: condition_predicates = array<i32: 5>
// SIM: condition_predicates = array<i32: 7>
// SIM: condition_predicates = array<i32: 9>
// SIM: condition_predicates = array<i32: 2>
// SIM: condition_predicates = array<i32: 4>
// SIM: condition_predicates = array<i32: 6>
// SIM: condition_predicates = array<i32: 8>
// SIM-NOT: timing_check_table
// CHECK: condition-zero 1 0 1 0 1
// CHECK-NEXT: condition-x 1 1 0 0 1
// CHECK-NEXT: computed-x 1 1 0 0
// CHECK-NEXT: condition-one 1 0 0 1 1
