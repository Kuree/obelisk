// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off},simulation.func(canonicalize,mem2reg,canonicalize)))' | FileCheck %s
// Mixed array/packed views must all become selections of the same SSA value.
// Array insert and packed insert retain their invalid/X/Z index semantics.
!array = !simulation.unpacked_array<0 : 1 x !simulation.logic<8>>
module {
  simulation.design @mixed_views {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "update"
    // CHECK-LABEL: simulation.func @update
    // CHECK-NOT: simulation.ref.alloc
    // CHECK: simulation.ref.load
    // CHECK: simulation.logic.dyn_insert
    // CHECK: simulation.array.insert_dynamic
    // CHECK: simulation.logic.dyn_insert
    // CHECK: simulation.array.insert_dynamic
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @update(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %source: !simulation.logic<2> {simulation.capture_kind = 2 : i32}, %cell: i32 {simulation.capture_kind = 2 : i32}, %bit: !simulation.logic<32> {simulation.capture_kind = 2 : i32}) -> !array attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %next = simulation.logic.binary add %bit, %one : !simulation.logic<32>
      %a = simulation.logic.extract %source from 0 : !simulation.logic<2> -> !simulation.logic<1>
      %b = simulation.logic.extract %source from 1 : !simulation.logic<2> -> !simulation.logic<1>
      %word = simulation.ref.array_element %root[%cell] : (!simulation.ref<!array>, i32) -> !simulation.ref<!simulation.logic<8>>
      %first = simulation.ref.dyn_extract %word from %bit : (!simulation.ref<!simulation.logic<8>>, !simulation.logic<32>) -> !simulation.ref<!simulation.logic<1>>
      %second = simulation.ref.dyn_extract %word from %next : (!simulation.ref<!simulation.logic<8>>, !simulation.logic<32>) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %a to %first : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %b to %second : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %value = simulation.ref.load %root : !simulation.ref<!array> -> !array
      simulation.return %value : !array
    }
  }
}
