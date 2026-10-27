// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-mark-bounded-loops)))' | FileCheck %s

// IEEE 1800-2023 11.4.5, 12.4, 12.7: canonicalization spells a four-state
// loop test `i != 0` as the truth value of `i`, and a case inequality as
// `logic.compare case_ne`. A truncating update is the low slice of the wider
// sum (11.6.1). For a known counter and a known bound each spelling is the
// same induction test, so the termination proof must not depend on which one
// the canonicalizer chose.
module {
  simulation.design @loops {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "truth"
    simulation.code_unit.decl 2 in 0 initial hierarchy "case_ne"
    simulation.code_unit.decl 3 in 0 initial hierarchy "unknown_bound"

    // CHECK-LABEL: simulation.func @truth
    // CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}} {schedule.bounded_loop_header}
    // CHECK: cf.br ^{{.*}}(%{{.*}} : !simulation.logic<4>) {schedule.bounded_loop_latch}
    simulation.func @truth(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %init = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      cf.br ^head(%init : !simulation.logic<4>)
    ^head(%i: !simulation.logic<4>):
      %more = simulation.logic.is_true %i : !simulation.logic<4>
      cf.cond_br %more, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %one = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
      %next = simulation.logic.binary sub %i, %one : !simulation.logic<4>
      cf.br ^head(%next : !simulation.logic<4>)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @case_ne
    // CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}} {schedule.bounded_loop_header}
    // CHECK: cf.br ^{{.*}}(%{{.*}} : !simulation.logic<4>) {schedule.bounded_loop_latch}
    simulation.func @case_ne(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %init = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      cf.br ^head(%init : !simulation.logic<4>)
    ^head(%i: !simulation.logic<4>):
      %bound = simulation.logic.constant 8 : i4, 0 : i4 : !simulation.logic<4>
      %more = simulation.logic.compare case_ne %i, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> i1
      cf.cond_br %more, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %wide = simulation.logic.resize %i signed = false : !simulation.logic<4> -> !simulation.logic<32>
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %sum = simulation.logic.binary add %wide, %one : !simulation.logic<32>
      %next = simulation.logic.extract %sum from 0 : !simulation.logic<32> -> !simulation.logic<4>
      cf.br ^head(%next : !simulation.logic<4>)
    ^exit:
      simulation.return
    }

    // A bound with X makes the comparison prove nothing about termination.
    // CHECK-LABEL: simulation.func @unknown_bound
    // CHECK-NOT: schedule.bounded_loop_header
    // CHECK: simulation.return
    simulation.func @unknown_bound(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %init = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      cf.br ^head(%init : !simulation.logic<4>)
    ^head(%i: !simulation.logic<4>):
      %bound = simulation.logic.constant 8 : i4, 1 : i4 : !simulation.logic<4>
      %more = simulation.logic.compare case_ne %i, %bound : (!simulation.logic<4>, !simulation.logic<4>) -> i1
      cf.cond_br %more, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %one = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
      %next = simulation.logic.binary add %i, %one : !simulation.logic<4>
      cf.br ^head(%next : !simulation.logic<4>)
    ^exit:
      simulation.return
    }
  }
}
