// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off}))' | FileCheck %s
// LRM 7.4.5: bounds and address identity are independent proofs. A reference
// remains bound through a diamond, but is recomputed on an outer backedge.
!array = !simulation.unpacked_array<0 : 3 x i32>
module {
  simulation.design @binding {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "diamond"
    simulation.code_unit.decl 2 in 0 function hierarchy "rebind"
    simulation.code_unit.decl 3 in 0 function hierarchy "reader"
    // CHECK-LABEL: simulation.func @diamond
    // CHECK: simulation.ref.store %[[VALUE:.*]] to
    // CHECK-NOT: simulation.ref.load
    // CHECK: simulation.return %[[VALUE]] : i32
    simulation.func @diamond(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i32 {simulation.capture_kind = 2 : i32}, %value: i32 {simulation.capture_kind = 2 : i32}, %choice: i1 {simulation.capture_kind = 2 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %mask = arith.constant 3 : i32
      %i = arith.andi %index, %mask : i32
      %ref = simulation.ref.array_element %root[%i] : (!simulation.ref<!array>, i32) -> !simulation.ref<i32>
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      cf.cond_br %choice, ^left, ^right
    ^left:
      cf.br ^join
    ^right:
      cf.br ^join
    ^join:
      %loaded = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return %loaded : i32
    }
    // CHECK-LABEL: simulation.func @rebind
    // CHECK: simulation.ref.array_element
    // CHECK: %[[OLD:.*]] = simulation.ref.load
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.load
    // CHECK: simulation.return
    simulation.func @rebind(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %four = arith.constant 4 : i32
      cf.br ^header(%zero, %zero : i32, i32)
    ^header(%i: i32, %sum: i32):
      %more = arith.cmpi ult, %i, %four : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ref = simulation.ref.array_element %root[%i] : (!simulation.ref<!array>, i32) -> !simulation.ref<i32>
      %old = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %ref : i32, !simulation.ref<i32>
      cf.br ^tail
    ^tail:
      %same = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      %total = arith.addi %sum, %same : i32
      %inc = arith.addi %i, %one : i32
      cf.br ^header(%inc, %total : i32, i32)
    ^exit:
      simulation.return %sum : i32
    }
    // This reader makes the canonical root shared, ruling out private SSA.
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !array attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %value = simulation.ref.load %root : !simulation.ref<!array> -> !array
      simulation.return %value : !array
    }
  }
}
