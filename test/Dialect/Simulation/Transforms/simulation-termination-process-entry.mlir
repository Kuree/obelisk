// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off}))' | FileCheck %s

// An event-triggered process starts and resumes only while the finish latch is
// clear. Static-storage accesses and calls of local-slot helpers cannot set
// it, so the polls after them are redundant. A callee that finishes still can.
!array = !simulation.unpacked_array<0 : 3 x i8>
module {
  simulation.design @process_entry {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "select"
    simulation.code_unit.decl 2 in 0 function hierarchy "finisher"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "comb"
    simulation.code_unit.decl 4 in 0 function hierarchy "subroutine"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "writes"
    simulation.func @select(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %values: !array {simulation.capture_kind = 2 : i32}, %i: i32 {simulation.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %local = simulation.ref.alloc %values : !array -> !simulation.ref<!array>
      %element = simulation.ref.array_element %local[%i] : (!simulation.ref<!array>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }
    simulation.func @finisher(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %x: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }

    // CHECK-LABEL: simulation.func @comb
    // CHECK-NOT: simulation.termination.requested
    // CHECK: simulation.suspend.change
    simulation.func @comb(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %values: !array {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %x = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %i = arith.extui %x : i8 to i32
      %y = simulation.call @select(%ctx, %values, %i) : (!simulation.context, !array, i32) -> i8
      %poll = simulation.termination.requested %ctx
      cf.cond_br %poll, ^done, ^wait
    ^wait:
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    ^done:
      simulation.return
    }

    // A subroutine can be entered after its caller requested termination.
    // CHECK-LABEL: simulation.func @subroutine
    // CHECK: simulation.termination.requested
    simulation.func @subroutine(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %values: !array {simulation.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %i = arith.constant 1 : i32
      %y = simulation.call @select(%ctx, %values, %i) : (!simulation.context, !array, i32) -> i8
      %poll = simulation.termination.requested %ctx
      cf.cond_br %poll, ^done, ^done
    ^done:
      simulation.return %y : i8
    }

    // CHECK-LABEL: simulation.func @writes
    // CHECK: simulation.call @finisher
    // CHECK-NEXT: simulation.termination.requested
    simulation.func @writes(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^body:
      %x = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.call @finisher(%ctx, %x) : (!simulation.context, i8) -> ()
      %poll = simulation.termination.requested %ctx
      cf.cond_br %poll, ^done, ^wait
    ^wait:
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    ^done:
      simulation.return
    }
  }
}
