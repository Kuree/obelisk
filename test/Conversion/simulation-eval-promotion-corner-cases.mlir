// RUN: talos-opt %s --schedule-promote-eval-storage | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,schedule-promote-eval-storage,talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' | FileCheck %s

// IEEE 1800-2023 6.8/6.21: default four-state values and initialization on
// each entry survive promotion. 7.3/7.3.1: union members share storage;
// different field views must not become independent values.
!pair = !simulation.unpacked_array<0 : 1 x !simulation.logic<8>>
!nested = !simulation.unpacked_array<0 : 31 x !pair>
!choice = !simulation.packed_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "alternate", type = i8, ordinal = 1, packedOffset = 0>
], isTagged = false>
module {
  simulation.design @corners {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "untouched_unknown"
    simulation.code_unit.decl 2 in 0 function hierarchy "conditional_field"
    simulation.code_unit.decl 3 in 0 function hierarchy "overlapping_union"
    simulation.code_unit.decl 4 in 0 function hierarchy "repeated_initializer"
    simulation.code_unit.decl 5 in 0 function hierarchy "nested_static_fields"

    // Writing one element must not turn the untouched element's X into zero.
    // CHECK-LABEL: simulation.func @untouched_unknown(
    // CHECK: %[[X:.*]] = simulation.logic.constant 0 : i8, -1 : i8
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.return %arg1, %[[X]]
    simulation.func @untouched_unknown(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32}) -> (!simulation.logic<8>, !simulation.logic<8>) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64, schedule.eval.borrowed_captures} {
      %initial = simulation.aggregate.default : !pair
      %local = simulation.ref.alloc %initial : !pair -> !simulation.ref<!pair>
      %a = simulation.ref.subelement %local[[0]] : !simulation.ref<!pair> -> !simulation.ref<!simulation.logic<8>>
      %b = simulation.ref.subelement %local[[1]] : !simulation.ref<!pair> -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %a : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %written = simulation.ref.load %a : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %untouched = simulation.ref.load %b : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %written, %untouched : !simulation.logic<8>, !simulation.logic<8>
    }

    // Both the value and unknown plane must come from the selected definition.
    // CHECK-LABEL: simulation.func @conditional_field(
    // CHECK: %[[INITIAL:.*]] = simulation.aggregate.default
    // CHECK: cf.cond_br %arg1, ^[[WRITE:bb[0-9]+]], ^[[JOIN:bb[0-9]+]](%[[INITIAL]]
    // CHECK: ^[[WRITE]]:
    // CHECK: %[[WRITTEN:.*]] = simulation.aggregate.insert %arg2 into %[[INITIAL]][1]
    // CHECK: cf.br ^[[JOIN]](%[[WRITTEN]]
    // CHECK: ^[[JOIN]](%[[MERGED:.*]]: !simulation.unpacked_array
    // CHECK-NOT: simulation.ref.
    // CHECK: %[[RESULT:.*]] = simulation.aggregate.extract %[[MERGED]][1]
    // CHECK: simulation.return %[[RESULT]]
    simulation.func @conditional_field(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}, %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64, schedule.eval.borrowed_captures} {
      %initial = simulation.aggregate.default : !pair
      %local = simulation.ref.alloc %initial : !pair -> !simulation.ref<!pair>
      %field = simulation.ref.subelement %local[[1]] : !simulation.ref<!pair> -> !simulation.ref<!simulation.logic<8>>
      cf.cond_br %choose, ^write, ^join
    ^write:
      simulation.ref.store %value to %field : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^join
    ^join:
      %result = simulation.ref.load %field : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %result : !simulation.logic<8>
    }

    // A write through one union member aliases the later read through another.
    // Whole-value forwarding must retain that alias and return the new value.
    // CHECK-LABEL: simulation.func @overlapping_union(
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.return %arg1 : i8
    simulation.func @overlapping_union(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64, schedule.eval.borrowed_captures} {
      %initial = simulation.aggregate.default : !choice
      %local = simulation.ref.alloc %initial : !choice -> !simulation.ref<!choice>
      %byte = simulation.ref.subelement %local[[0]] : !simulation.ref<!choice> -> !simulation.ref<i8>
      %word = simulation.ref.subelement %local[[1]] : !simulation.ref<!choice> -> !simulation.ref<i8>
      simulation.ref.store %value to %byte : i8, !simulation.ref<i8>
      %result = simulation.ref.load %word : !simulation.ref<i8> -> i8
      simulation.return %result : i8
    }

    // A slot allocated in a cycle starts with %initial each time. The promoted
    // update depends on that capture, not on a slot value from the prior trip.
    // CHECK-LABEL: simulation.func @repeated_initializer(
    // CHECK: %[[ONE:.*]] = arith.constant 1 : i8
    // CHECK: cf.br ^[[LOOP:bb[0-9]+]]
    // CHECK: ^[[LOOP]]:
    // CHECK-NOT: simulation.ref.alloc
    // CHECK-NOT: simulation.ref.load
    // CHECK: %[[NEXT:.*]] = arith.addi %arg1, %[[ONE]] : i8
    // CHECK: simulation.ref.store %[[NEXT]]
    // CHECK-NOT: simulation.ref.load
    // CHECK: cf.cond_br %arg2, ^[[LOOP]], ^[[DONE:bb[0-9]+]]
    // CHECK: ^[[DONE]]:
    // CHECK: simulation.return
    simulation.func @repeated_initializer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %initial: i8 {simulation.capture_kind = 1 : i32}, %again: i1 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64, schedule.eval.borrowed_captures} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i8
      cf.br ^loop(%zero : i32)
    ^loop(%iteration: i32):
      %local = simulation.ref.alloc %initial : i8 -> !simulation.ref<i8>
      %old = simulation.ref.load %local : !simulation.ref<i8> -> i8
      %next = arith.addi %old, %one : i8
      simulation.ref.store %next to %local : i8, !simulation.ref<i8>
      %loaded = simulation.ref.load %local : !simulation.ref<i8> -> i8
      simulation.ref.store %loaded to %out : i8, !simulation.ref<i8>
      %step = arith.constant 1 : i32
      %next_iteration = arith.addi %iteration, %step : i32
      cf.cond_br %again, ^loop(%next_iteration : i32), ^done
    ^done:
      simulation.return
    }

    // A large parent cannot use whole-value promotion. Recursive static-field
    // SROA must preserve declaration-order paths and defaults of sibling fields.
    // CHECK-LABEL: simulation.func @nested_static_fields(
    // CHECK: %[[X:.*]] = simulation.logic.constant 0 : i8, -1 : i8
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.return %arg1, %[[X]]
    simulation.func @nested_static_fields(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32}) -> (!simulation.logic<8>, !simulation.logic<8>) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64, schedule.eval.borrowed_captures} {
      %initial = simulation.aggregate.default : !nested
      %local = simulation.ref.alloc %initial : !nested -> !simulation.ref<!nested>
      %a = simulation.ref.subelement %local[[11, 0]] : !simulation.ref<!nested> -> !simulation.ref<!simulation.logic<8>>
      %b = simulation.ref.subelement %local[[11, 1]] : !simulation.ref<!nested> -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %a : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %written = simulation.ref.load %a : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %untouched = simulation.ref.load %b : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %written, %untouched : !simulation.logic<8>, !simulation.logic<8>
    }
  }
}
