// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-mark-bounded-loops)))' | FileCheck %s

// Canonicalization folds the empty join of `if (en) q <= 0;` into the header
// of a following `for` loop, which then has two entry edges. Both start the
// induction at the same constant, so they are one entry and the loop keeps
// its termination proof. Entries that start at different constants do not
// describe one induction sequence and are left unmarked.
module {
  simulation.design @loops {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "merged"
    simulation.code_unit.decl 2 in 0 initial hierarchy "different"

    // CHECK-LABEL: simulation.func @merged
    // CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}} {schedule.bounded_loop_header}
    // CHECK: cf.br ^{{.*}}(%{{.*}} : i32) {schedule.bounded_loop_latch}
    simulation.func @merged(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %flag: !simulation.ref<i1> {simulation.capture_kind = 1 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %en = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      cf.cond_br %en, ^then, ^head(%zero : i32)
    ^then:
      %again = arith.constant 0 : i32
      cf.br ^head(%again : i32)
    ^head(%i: i32):
      %limit = arith.constant 81 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @different
    // CHECK-NOT: bounded_loop
    // CHECK: simulation.return
    simulation.func @different(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %flag: !simulation.ref<i1> {simulation.capture_kind = 1 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      %en = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      cf.cond_br %en, ^then, ^head(%zero : i32)
    ^then:
      %five = arith.constant 5 : i32
      cf.br ^head(%five : i32)
    ^head(%i: i32):
      %limit = arith.constant 81 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
