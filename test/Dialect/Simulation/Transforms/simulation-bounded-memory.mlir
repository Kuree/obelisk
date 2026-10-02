// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off}))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=full}))' | FileCheck %s --check-prefix=FULL
!array = !simulation.unpacked_array<0 : 3 x i16>
module {
  simulation.design @bounded_memory {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "bounded"
    simulation.code_unit.decl 2 in 0 function hierarchy "invalid"
    // Equal SSA address plus an in-bounds integer-range proof permits
    // forwarding. The root's may-access hull alone does not establish equality.
    // CHECK-LABEL: simulation.func @bounded
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.load
    // CHECK: simulation.return %arg2
    // FULL-LABEL: simulation.func @bounded
    // FULL: simulation.ref.load
    simulation.func @bounded(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: i32 {simulation.capture_kind = 2 : i32}, %value: i16 {simulation.capture_kind = 2 : i32}) -> i16 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %three = arith.constant 3 : i32
      %index = arith.andi %selector, %three : i32
      %cell = simulation.ref.array_element %root[%index] : (!simulation.ref<!array>, i32) -> !simulation.ref<i16>
      simulation.ref.store %value to %cell : i16, !simulation.ref<i16>
      %after = simulation.ref.load %cell : !simulation.ref<i16> -> i16
      simulation.return %after : i16
    }
    // An out-of-bounds store is a no-op and the read returns its IEEE default.
    // CHECK-LABEL: simulation.func @invalid
    // CHECK: simulation.ref.store
    // CHECK: simulation.ref.load
    simulation.func @invalid(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: i32 {simulation.capture_kind = 2 : i32}, %value: i16 {simulation.capture_kind = 2 : i32}) -> i16 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %cell = simulation.ref.array_element %root[%selector] : (!simulation.ref<!array>, i32) -> !simulation.ref<i16>
      simulation.ref.store %value to %cell : i16, !simulation.ref<i16>
      %after = simulation.ref.load %cell : !simulation.ref<i16> -> i16
      simulation.return %after : i16
    }
  }
}
