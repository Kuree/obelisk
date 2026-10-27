// RUN: talos-opt %s -o /dev/null --pass-pipeline='builtin.module(test-talos-sim-state-domain)' 2>&1 | FileCheck %s

// A dynamic selection with a valid index only moves known bits; an invalid
// index selects X bits or the X element default (IEEE 1800-2023 7.4.6,
// 11.5.1). An insertion with an invalid index performs no write, so the index
// never affects its domain.

!array = !simulation.packed_array<3 : 0 x !simulation.logic<2>>

module {
  simulation.design @dynamic_selection {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.dynamic_selection.rules"
    simulation.scope.decl 0
    simulation.func @rules(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %runtime: i8 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %bits = arith.constant 165 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %array = simulation.packed.unflatten %known : (!simulation.logic<8>) -> !array
      %two = arith.constant 2 : i8
      %seven = arith.constant 7 : i8
      %one = arith.constant 1 : i2
      %element = simulation.logic.from_bits %one : i2 -> !simulation.logic<2>
      %in_range = simulation.array.extract_dynamic %array[%two] : (!array, i8) -> !simulation.logic<2>
      %outside = simulation.array.extract_dynamic %array[%seven] : (!array, i8) -> !simulation.logic<2>
      %runtime_element = simulation.array.extract_dynamic %array[%runtime] : (!array, i8) -> !simulation.logic<2>
      %inserted = simulation.array.insert_dynamic %element into %array[%runtime] : (!array, !simulation.logic<2>, i8) -> !array
      %bit = simulation.logic.dyn_extract %known from %two : (!simulation.logic<8>, i8) -> !simulation.logic<1>
      %runtime_bit = simulation.logic.dyn_extract %known from %runtime : (!simulation.logic<8>, i8) -> !simulation.logic<1>
      %replacement = simulation.logic.from_bits %one : i2 -> !simulation.logic<2>
      %patched = simulation.logic.dyn_insert %replacement into %known at %runtime : (!simulation.logic<8>, !simulation.logic<2>, i8) -> !simulation.logic<8>
      simulation.return
    }
  }
}

// CHECK-LABEL: func @rules
// CHECK: bb0.op1.result0: two-state (logic-from-bits)
// CHECK-NEXT: bb0.op2.result0: two-state (packed-view)
// CHECK-NEXT: bb0.op6.result0: two-state (logic-from-bits)
// CHECK-NEXT: bb0.op7.result0: two-state (dynamic-extract)
// CHECK-NEXT: bb0.op8.result0: may-four-state (dynamic-extract-index)
// CHECK-NEXT: bb0.op9.result0: may-four-state (dynamic-extract-index)
// CHECK-NEXT: bb0.op10.result0: two-state (dynamic-extract)
// CHECK-NEXT: bb0.op11.result0: two-state (dynamic-extract)
// CHECK-NEXT: bb0.op12.result0: may-four-state (dynamic-extract-index)
// CHECK-NEXT: bb0.op13.result0: two-state (logic-from-bits)
// CHECK-NEXT: bb0.op14.result0: two-state (dynamic-extract)
