// RUN: talos-opt %s --schedule-localize-private-eval-storage | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(schedule-localize-private-eval-storage,schedule-promote-eval-storage))' | FileCheck %s --check-prefix=SSA

// The paired canonical actor and borrowed evaluator have equivalent bodies.
// The evaluator uses separate returns instead of the actor's common wait.
// Every return must publish its final value (LRM 4.6 and 10.4.1), including
// a branch that preserves the incoming root. Entry captures avoid creating
// a second canonical reference, and an unrelated pure call is permitted.
module {
  simulation.design @exits {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design {observability = 0 : i32}
    simulation.storage.decl 1 in 0 : i8 design {observability = 0 : i32}
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "actor"
    simulation.code_unit.decl 2 in 0 function hierarchy "eval"
    simulation.code_unit.decl 3 in 0 function hierarchy "observer"
    simulation.code_unit.decl 4 in 0 function hierarchy "identity"
    simulation.func @observer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %r = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %v = simulation.ref.load %r : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.func private @identity(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %v: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      simulation.return %v : i8
    }
    simulation.func @actor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.body = @eval} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %test = arith.cmpi ne, %v, %zero : i8
      cf.cond_br %test, ^write, ^wait
    ^write:
      %called = simulation.call @identity(%ctx, %v) : (!simulation.context, i8) -> i8
      simulation.ref.store %called to %out : i8, !simulation.ref<i8>
      %old = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %next = arith.addi %old, %v : i8
      simulation.ref.store %next to %out : i8, !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    // CHECK-LABEL: simulation.func @eval(
    // CHECK-SAME: %[[OUT:[a-zA-Z0-9_]+]]: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}
    // CHECK: %[[IN:.*]] = simulation.ref.load %[[OUT]]
    // CHECK-NEXT: %[[LOCAL:.*]] = simulation.ref.alloc %[[IN]]
    // CHECK: simulation.call @identity
    // CHECK: simulation.ref.store %{{.*}} to %[[LOCAL]]
    // CHECK: simulation.ref.load %[[LOCAL]]
    // CHECK: simulation.ref.store %{{.*}} to %[[LOCAL]]
    // CHECK: %[[FINAL:.*]] = simulation.ref.load %[[LOCAL]]
    // CHECK-NEXT: simulation.ref.store %[[FINAL]] to %[[OUT]]
    // CHECK-NEXT: simulation.return
    // CHECK: %[[UNCHANGED:.*]] = simulation.ref.load %[[LOCAL]]
    // CHECK-NEXT: simulation.ref.store %[[UNCHANGED]] to %[[OUT]]
    // CHECK-NEXT: simulation.return
    // SSA-LABEL: simulation.func @eval(
    // SSA-SAME: %[[OUT:[a-zA-Z0-9_]+]]: !simulation.ref<i8>
    // SSA-NOT: simulation.ref.alloc
    // SSA: %[[INCOMING:.*]] = simulation.ref.load %[[OUT]]
    // SSA: %[[INPUT:.*]] = simulation.ref.load
    // SSA: cf.cond_br %{{.*}}, ^[[WRITE:bb[0-9]+]], ^[[UNCHANGED:bb[0-9]+]]
    // SSA: ^[[WRITE]]:
    // SSA-NEXT: %[[CALLED:.*]] = simulation.call @identity(%arg0, %[[INPUT]])
    // SSA-NEXT: %[[FINAL:.*]] = arith.addi %[[CALLED]], %[[INPUT]] : i8
    // SSA-NEXT: simulation.ref.store %[[FINAL]] to %[[OUT]]
    // SSA-NEXT: simulation.return
    // SSA: ^[[UNCHANGED]]:
    // SSA-NEXT: simulation.ref.store %[[INCOMING]] to %[[OUT]]
    // SSA-NEXT: simulation.return
    simulation.func @eval(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64, schedule.eval.borrowed_captures} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %test = arith.cmpi ne, %v, %zero : i8
      cf.cond_br %test, ^write, ^unchanged
    ^write:
      %called = simulation.call @identity(%ctx, %v) : (!simulation.context, i8) -> i8
      simulation.ref.store %called to %out : i8, !simulation.ref<i8>
      %old = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %next = arith.addi %old, %v : i8
      simulation.ref.store %next to %out : i8, !simulation.ref<i8>
      simulation.return
    ^unchanged:
      simulation.return
    }
  }
}
