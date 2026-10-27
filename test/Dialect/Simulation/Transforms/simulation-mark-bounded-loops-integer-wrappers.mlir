// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-mark-bounded-loops)))' | FileCheck %s

// IEEE 1800-2023 11.6.1, 12.7: comparison promotion does not widen the
// induction variable's storage. Recognize the two-state packed recurrence,
// but prove termination at its original width, including the exit step.
!counter = !simulation.packed_array<5 : 0 x i1>
module {
  simulation.design @integer_wrappers {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "bounded"
    // CHECK-LABEL: simulation.func @bounded
    // CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}} {schedule.bounded_loop_header}
    // CHECK: cf.br ^{{.*}}(%{{.*}} : !simulation.packed_array<5 : 0 x i1>) {schedule.bounded_loop_latch}
    simulation.func @bounded(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i6
      %initial = simulation.packed.unflatten %zero : (i6) -> !counter
      %one = arith.constant 1 : i32
      %limit = arith.constant 31 : i32
      cf.br ^head(%initial : !counter)
    ^head(%i: !counter):
      %bits = simulation.packed.flatten %i : (!counter) -> i6
      %wide = arith.extui %bits : i6 to i32
      %more = arith.cmpi ule, %wide, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %sum = arith.addi %wide, %one : i32
      %nextBits = arith.trunci %sum : i32 to i6
      %next = simulation.packed.unflatten %nextBits : (i6) -> !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }
    simulation.code_unit.decl 2 in 0 initial hierarchy "reversed"
    // CHECK-LABEL: simulation.func @reversed
    // CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}} {schedule.bounded_loop_header}
    // CHECK: cf.br ^{{.*}}(%{{.*}} : !simulation.packed_array<5 : 0 x i1>) {schedule.bounded_loop_latch}
    simulation.func @reversed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i6
      %initial = simulation.packed.unflatten %zero : (i6) -> !counter
      %one = arith.constant 1 : i32
      %limit = arith.constant 31 : i32
      cf.br ^head(%initial : !counter)
    ^head(%i: !counter):
      %bits = simulation.packed.flatten %i : (!counter) -> i6
      %wide = arith.extui %bits : i6 to i32
      %more = arith.cmpi uge, %limit, %wide : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %sum = arith.addi %wide, %one : i32
      %nextBits = arith.trunci %sum : i32 to i6
      %next = simulation.packed.unflatten %nextBits : (i6) -> !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }
    simulation.code_unit.decl 3 in 0 initial hierarchy "wrapping"
    // CHECK-LABEL: simulation.func @wrapping
    // CHECK-NOT: schedule.bounded_loop_
    // CHECK: simulation.return
    simulation.func @wrapping(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i6
      %initial = simulation.packed.unflatten %zero : (i6) -> !counter
      %one = arith.constant 1 : i32
      %limit = arith.constant 63 : i32
      cf.br ^head(%initial : !counter)
    ^head(%i: !counter):
      %bits = simulation.packed.flatten %i : (!counter) -> i6
      %wide = arith.extui %bits : i6 to i32
      %more = arith.cmpi ule, %wide, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %sum = arith.addi %wide, %one : i32
      %nextBits = arith.trunci %sum : i32 to i6
      %next = simulation.packed.unflatten %nextBits : (i6) -> !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }
    simulation.code_unit.decl 4 in 0 initial hierarchy "outside_bound"
    // CHECK-LABEL: simulation.func @outside_bound
    // CHECK-NOT: schedule.bounded_loop_
    // CHECK: simulation.return
    simulation.func @outside_bound(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i6
      %initial = simulation.packed.unflatten %zero : (i6) -> !counter
      %one = arith.constant 1 : i32
      %limit = arith.constant 64 : i32
      cf.br ^head(%initial : !counter)
    ^head(%i: !counter):
      %bits = simulation.packed.flatten %i : (!counter) -> i6
      %wide = arith.extui %bits : i6 to i32
      %more = arith.cmpi ult, %wide, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %sum = arith.addi %wide, %one : i32
      %nextBits = arith.trunci %sum : i32 to i6
      %next = simulation.packed.unflatten %nextBits : (i6) -> !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }
    simulation.code_unit.decl 5 in 0 initial hierarchy "narrowed_test"
    // CHECK-LABEL: simulation.func @narrowed_test
    // CHECK-NOT: schedule.bounded_loop_
    // CHECK: simulation.return
    simulation.func @narrowed_test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %zero = arith.constant 0 : i6
      %initial = simulation.packed.unflatten %zero : (i6) -> !counter
      %one = arith.constant 1 : i32
      %limit = arith.constant 15 : i32
      cf.br ^head(%initial : !counter)
    ^head(%i: !counter):
      %bits = simulation.packed.flatten %i : (!counter) -> i6
      %low = arith.trunci %bits : i6 to i4
      %wide = arith.extui %low : i4 to i32
      %more = arith.cmpi ule, %wide, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %sum = arith.addi %wide, %one : i32
      %nextBits = arith.trunci %sum : i32 to i6
      %next = simulation.packed.unflatten %nextBits : (i6) -> !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }
  }
}
