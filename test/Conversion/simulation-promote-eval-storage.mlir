// RUN: obelisk-opt %s --schedule-promote-eval-storage | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(schedule-promote-eval-storage,schedule-promote-eval-storage))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-promote-eval-storage,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// This is the production cleanup after late localization. Canonical actors
// keep their threaded continuation operands. Local SSA still observes blocking
// assignment order (LRM 4.6/10.4.1); promotion cannot remove an escaped address.
!array = !simulation.unpacked_array<3 : 2 x i8>
module {
  simulation.design @promotion {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 function hierarchy "ordinary"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "canonical"
    simulation.code_unit.decl 3 in 0 function hierarchy "scalar"
    simulation.code_unit.decl 4 in 0 function hierarchy "join"
    simulation.code_unit.decl 5 in 0 function hierarchy "escape"
    simulation.code_unit.decl 6 in 0 function hierarchy "consume"
    simulation.code_unit.decl 7 in 0 function hierarchy "fixed_array"
    simulation.code_unit.decl 8 in 0 function hierarchy "dynamic_array"
    simulation.code_unit.decl 9 in 0 function hierarchy "reference_roots"

    // CHECK-LABEL: simulation.func @ordinary(
    // CHECK: %[[ORDINARY:.*]] = simulation.ref.alloc
    // CHECK: simulation.ref.store %arg1 to %[[ORDINARY]]
    // CHECK: simulation.ref.load %[[ORDINARY]]
    // CHECK: simulation.return
    simulation.func @ordinary(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i32 {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %local = simulation.ref.alloc %zero : i32 -> !simulation.ref<i32>
      simulation.ref.store %value to %local : i32, !simulation.ref<i32>
      %loaded = simulation.ref.load %local : !simulation.ref<i32> -> i32
      simulation.return %loaded : i32
    }

    // CHECK-LABEL: simulation.func @canonical(
    // CHECK: cf.br ^bb1
    // CHECK: ^bb1:
    // CHECK: %[[CANON:.*]] = simulation.ref.alloc
    // CHECK: simulation.ref.load %[[CANON]]
    // CHECK: simulation.ref.store %{{.*}} to %[[CANON]]
    // CHECK: simulation.suspend.change %arg1 to ^bb1
    simulation.func @canonical(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %watched: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %local = simulation.ref.alloc %zero : i32 -> !simulation.ref<i32>
      %old = simulation.ref.load %local : !simulation.ref<i32> -> i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %local : i32, !simulation.ref<i32>
      simulation.suspend.change %watched to ^body : !simulation.ref<i1>
    }

    // CHECK-LABEL: simulation.func @scalar(
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.return %arg1 : i32
    simulation.func @scalar(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i32 {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64, schedule.eval.borrowed_captures} {
      %zero = arith.constant 0 : i32
      %local = simulation.ref.alloc %zero : i32 -> !simulation.ref<i32>
      simulation.ref.store %value to %local : i32, !simulation.ref<i32>
      %loaded = simulation.ref.load %local : !simulation.ref<i32> -> i32
      simulation.return %loaded : i32
    }

    // CHECK-LABEL: simulation.func @join(
    // CHECK-NOT: simulation.ref.
    // CHECK: %[[MERGED:.*]] = arith.select %arg1, %arg3, %arg2 : i32
    // CHECK-NEXT: simulation.return %[[MERGED]] : i32
    simulation.func @join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}, %incoming: i32 {simulation.capture_kind = 1 : i32}, %replacement: i32 {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64, schedule.eval.borrowed_captures} {
      %local = simulation.ref.alloc %incoming : i32 -> !simulation.ref<i32>
      cf.cond_br %choose, ^write, ^join
    ^write:
      simulation.ref.store %replacement to %local : i32, !simulation.ref<i32>
      cf.br ^join
    ^join:
      %value = simulation.ref.load %local : !simulation.ref<i32> -> i32
      simulation.return %value : i32
    }

    // CHECK-LABEL: simulation.func @escape(
    // CHECK: %[[ESCAPED:.*]] = simulation.ref.alloc
    // CHECK: simulation.call @consume(%arg0, %[[ESCAPED]])
    // CHECK: simulation.ref.load %[[ESCAPED]]
    // CHECK: simulation.return
    simulation.func @escape(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i32 {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64, schedule.eval.borrowed_captures} {
      %local = simulation.ref.alloc %value : i32 -> !simulation.ref<i32>
      simulation.call @consume(%ctx, %local) : (!simulation.context, !simulation.ref<i32>) -> ()
      %loaded = simulation.ref.load %local : !simulation.ref<i32> -> i32
      simulation.return %loaded : i32
    }
    simulation.func private @consume(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r: !simulation.ref<i32> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %one = arith.constant 1 : i32
      simulation.ref.store %one to %r : i32, !simulation.ref<i32>
      simulation.return
    }

    // CHECK-LABEL: simulation.func @fixed_array(
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.return %arg1 : i8
    simulation.func @fixed_array(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64, schedule.eval.borrowed_captures} {
      %initial = simulation.aggregate.default : !array
      %local = simulation.ref.alloc %initial : !array -> !simulation.ref<!array>
      %two = arith.constant 2 : i32
      %element = simulation.ref.array_element %local[%two] : (!simulation.ref<!array>, i32) -> !simulation.ref<i8>
      simulation.ref.store %value to %element : i8, !simulation.ref<i8>
      %loaded = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // CHECK-LABEL: simulation.func @dynamic_array(
    // CHECK: %[[DYNAMIC:.*]] = simulation.ref.alloc
    // CHECK: simulation.ref.array_element %[[DYNAMIC]][%arg1]
    // CHECK: simulation.return
    simulation.func @dynamic_array(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i32 {simulation.capture_kind = 1 : i32}, %value: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64, schedule.eval.borrowed_captures} {
      %initial = simulation.aggregate.default : !array
      %local = simulation.ref.alloc %initial : !array -> !simulation.ref<!array>
      %element = simulation.ref.array_element %local[%index] : (!simulation.ref<!array>, i32) -> !simulation.ref<i8>
      simulation.ref.store %value to %element : i8, !simulation.ref<i8>
      %loaded = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // CHECK-LABEL: simulation.func @reference_roots(
    // CHECK-DAG: %[[A:.*]] = simulation.context.storage %arg0[1]
    // CHECK-DAG: %[[B:.*]] = simulation.context.storage %arg0[2]
    // CHECK: cf.cond_br %arg1
    // CHECK: simulation.nba.enqueue %arg2 to %[[A]]
    // CHECK: simulation.return
    // CHECK: simulation.nba.enqueue %arg2 to %[[B]]
    // CHECK: simulation.return
    simulation.func @reference_roots(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}, %value: i32 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64, schedule.eval.borrowed_captures} {
      %a = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %b = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      %local = simulation.ref.alloc %value : i32 -> !simulation.ref<i32>
      cf.cond_br %choose, ^left, ^right
    ^left:
      %l = simulation.ref.load %local : !simulation.ref<i32> -> i32
      simulation.nba.enqueue %l to %a : (i32, !simulation.ref<i32>) -> ()
      simulation.return
    ^right:
      %r = simulation.ref.load %local : !simulation.ref<i32> -> i32
      simulation.nba.enqueue %r to %b : (i32, !simulation.ref<i32>) -> ()
      simulation.return
    }
  }
}
