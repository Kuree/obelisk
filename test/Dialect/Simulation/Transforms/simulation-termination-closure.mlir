// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off}))' | FileCheck %s
module {
  simulation.design @polls {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 function hierarchy "leaf"
    simulation.code_unit.decl 2 in 0 function hierarchy "helper"
    simulation.code_unit.decl 3 in 0 function hierarchy "writer"
    simulation.code_unit.decl 4 in 0 initial hierarchy "polls"
    simulation.code_unit.decl 5 in 0 function hierarchy "observe"
    simulation.func @observe(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %value = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return %value : i32
    }
    simulation.func @leaf(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %x: i32 {simulation.capture_kind = 2 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %y = arith.addi %x, %x : i32
      simulation.return %y : i32
    }
    simulation.func @helper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %x: i32 {simulation.capture_kind = 2 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %y = simulation.call @leaf(%ctx, %x) : (!simulation.context, i32) -> i32
      simulation.return %y : i32
    }
    simulation.func @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %x: i32 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      simulation.ref.store %x to %ref : i32, !simulation.ref<i32>
      simulation.return
    }
    // CHECK-LABEL: simulation.func @polls
    // CHECK: simulation.termination.requested
    // CHECK: simulation.call @helper
    // CHECK: arith.constant false
    // CHECK: simulation.call @writer
    // CHECK: simulation.termination.requested
    simulation.func @polls(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %first = simulation.termination.requested %ctx
      cf.cond_br %first, ^done, ^clear
    ^clear:
      %x = arith.constant 1 : i32
      %y = simulation.call @helper(%ctx, %x) : (!simulation.context, i32) -> i32
      %second = simulation.termination.requested %ctx
      cf.cond_br %second, ^done, ^write
    ^write:
      simulation.call @writer(%ctx, %y) : (!simulation.context, i32) -> ()
      %third = simulation.termination.requested %ctx
      cf.cond_br %third, ^done, ^done
    ^done:
      simulation.return
    }
  }
}
