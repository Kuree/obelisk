// RUN: obelisk-opt %s '--pass-pipeline=builtin.module(simulation.design(simulation.func(schedule-lower-conditional-reference-accesses)))' -o %t.lowered
// RUN: FileCheck %s < %t.lowered
// RUN: obelisk-opt %t.lowered '--test-obelisk-local-storage=functions=conditional allocations=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=PRIVATE
// RUN: obelisk-opt %t.lowered '--pass-pipeline=builtin.module(simulation.design(simulation.func(schedule-lower-conditional-reference-accesses)))' -o %t.again
// RUN: diff %t.lowered %t.again

// Selection captures an address, not its value. A store between selection and
// read must remain before either load. The unselected handle is not read.
module {
  simulation.design @conditional_reads {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "conditional"
    // CHECK-LABEL: simulation.func @conditional
    // CHECK: simulation.ref.load %{{.*}} {test.unrelated}
    // CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
    // CHECK-NOT: arith.select
    // CHECK: simulation.ref.store %{{.*}} to %[[LOCAL]]
    // CHECK-NEXT: %[[RESULT:.*]] = scf.if %{{.*}} -> (i8) {
    // CHECK-NEXT: %[[A:.*]] = simulation.ref.load %[[LOCAL]] {test.keep}
    // CHECK-NEXT: scf.yield %[[A]] : i8
    // CHECK-NEXT: } else {
    // CHECK-NEXT: %[[B:.*]] = simulation.ref.load %{{.*}} {test.keep}
    // CHECK-NEXT: scf.yield %[[B]] : i8
    // CHECK-NEXT: }
    // CHECK-NEXT: simulation.ref.store %[[RESULT]] to
    // PRIVATE: local allocation
    // PRIVATE-NOT: runtime
    simulation.func @conditional(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %global = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      // The late pass must not perform general dead-code cleanup on an
      // already planned body, even when another read is rewritten.
      %unused = simulation.ref.load %global {test.unrelated} : !simulation.ref<i8> -> i8
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %slot = simulation.ref.alloc %zero : i8 -> !simulation.ref<i8>
      %selected = arith.select %choose, %slot, %global : !simulation.ref<i8>
      simulation.ref.store %one to %slot : i8, !simulation.ref<i8>
      %value = simulation.ref.load %selected {test.keep} : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.return
    }

    simulation.storage.decl 2 in 0 : !simulation.unpacked_array<0 : 1 x i8> design
    simulation.code_unit.decl 2 in 0 function hierarchy "projected"
    // A pure view can be rebuilt in each selected arm using the original SSA
    // index. Out-of-range and X/Z index behavior belongs to the selected load.
    // CHECK-LABEL: simulation.func @projected
    // CHECK-NOT: arith.select
    // CHECK: scf.if
    // CHECK: simulation.ref.array_element
    // CHECK-NEXT: %{{.*}} = simulation.ref.load
    // CHECK-NEXT: scf.yield
    // CHECK: simulation.ref.array_element
    // CHECK-NEXT: %{{.*}} = simulation.ref.load
    // CHECK-NEXT: scf.yield
    simulation.func @projected(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}, %index: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %global = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %initial = simulation.aggregate.default : !simulation.unpacked_array<0 : 1 x i8>
      %slot = simulation.ref.alloc %initial : !simulation.unpacked_array<0 : 1 x i8> -> !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %selected = arith.select %choose, %slot, %global : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %view = simulation.ref.array_element %selected[%index] : (!simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>, !simulation.logic<8>) -> !simulation.ref<i8>
      %value = simulation.ref.load %view : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.return
    }

    simulation.code_unit.decl 3 in 0 function hierarchy "write_selection"
    // Writes execute only in the selected arm, preserving their value snapshot.
    // CHECK-LABEL: simulation.func @write_selection
    // CHECK-NOT: arith.select
    // CHECK: scf.if
    // CHECK-NEXT: simulation.ref.store
    // CHECK: } else {
    // CHECK-NEXT: simulation.ref.store
    simulation.func @write_selection(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %global = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %slot = simulation.ref.alloc %zero : i8 -> !simulation.ref<i8>
      %selected = arith.select %choose, %slot, %global : !simulation.ref<i8>
      simulation.ref.store %zero to %selected : i8, !simulation.ref<i8>
      simulation.return
    }

    simulation.code_unit.decl 4 in 0 function hierarchy "structured"
    // Nested structured regions retain their single-block contract.
    // CHECK-LABEL: simulation.func @structured
    // CHECK: scf.if
    // CHECK-NOT: arith.select
    // CHECK: scf.if
    // CHECK-NEXT: %{{.*}} = simulation.ref.load
    simulation.func @structured(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %outer: i1 {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %global = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %slot = simulation.ref.alloc %zero : i8 -> !simulation.ref<i8>
      scf.if %outer {
        %selected = arith.select %choose, %slot, %global : !simulation.ref<i8>
        %value = simulation.ref.load %selected : !simulation.ref<i8> -> i8
        simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      }
      simulation.return
    }

    simulation.code_unit.decl 5 in 0 function hierarchy "nested_selection"
    // Rewriting a load from nested selections must process the newly created
    // inner read, and remove both unused reference selections.
    // CHECK-LABEL: simulation.func @nested_selection
    // CHECK-NOT: arith.select
    // CHECK: scf.if
    // CHECK-NEXT: %{{.*}} = scf.if
    // CHECK-NEXT: %{{.*}} = simulation.ref.load
    // CHECK: simulation.return
    simulation.func @nested_selection(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: i1 {simulation.capture_kind = 2 : i32}, %b: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %global = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %slot = simulation.ref.alloc %zero : i8 -> !simulation.ref<i8>
      %inner = arith.select %a, %slot, %global : !simulation.ref<i8>
      %outer = arith.select %b, %inner, %global : !simulation.ref<i8>
      %value = simulation.ref.load %outer : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.return
    }
  }
}
