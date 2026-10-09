// RUN: obelisk-opt %s --schedule-promote-eval-storage | FileCheck %s

// Split independent static fields, while preserving whole aggregate copies,
// escaped addresses and reference choices. SROA reuses the original initializer
// and member indices (LRM 6.21/7.4.5); blocking assignment order stays intact.
!array = !simulation.unpacked_array<0 : 31 x i32>
!inner = !simulation.unpacked_array<0 : 1 x i32>
!nested = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @fields {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 function hierarchy "mixed"
    simulation.code_unit.decl 2 in 0 function hierarchy "nested"
    simulation.code_unit.decl 3 in 0 function hierarchy "escaped"
    simulation.code_unit.decl 4 in 0 function hierarchy "consume"
    simulation.code_unit.decl 5 in 0 function hierarchy "reference_choice"
    simulation.code_unit.decl 6 in 0 function hierarchy "dynamic_child"

    // A whole read plus a field update stays compact in native storage.
    // CHECK-LABEL: simulation.func @mixed(
    // CHECK: %[[LOCAL:.*]] = simulation.ref.alloc %arg1
    // CHECK: %[[FIELD:.*]] = simulation.ref.subelement %[[LOCAL]]
    // CHECK: simulation.ref.store %arg2 to %[[FIELD]]
    // CHECK: %[[WHOLE:.*]] = simulation.ref.load %[[LOCAL]]
    // CHECK-NEXT: simulation.return %[[WHOLE]]
    simulation.func @mixed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %incoming: !array {simulation.capture_kind = 1 : i32}, %value: i32 {simulation.capture_kind = 1 : i32}) -> !array attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64, schedule.eval.borrowed_captures} {
      %local = simulation.ref.alloc %incoming : !array -> !simulation.ref<!array>
      %field = simulation.ref.subelement %local[[3]] : !simulation.ref<!array> -> !simulation.ref<i32>
      simulation.ref.store %value to %field : i32, !simulation.ref<i32>
      %whole = simulation.ref.load %local : !simulation.ref<!array> -> !array
      simulation.return %whole : !array
    }

    // Destructuring the outer field exposes a whole child for mem2reg. Do not
    // recursively split that child's whole values into scalar elements.
    // CHECK-LABEL: simulation.func @nested(
    // CHECK-NOT: simulation.ref.
    // CHECK-NOT: simulation.aggregate.
    // CHECK: simulation.return %arg1
    simulation.func @nested(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !inner {simulation.capture_kind = 1 : i32}) -> !inner attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64, schedule.eval.borrowed_captures} {
      %initial = simulation.aggregate.default : !nested
      %local = simulation.ref.alloc %initial : !nested -> !simulation.ref<!nested>
      %field = simulation.ref.subelement %local[[1]] : !simulation.ref<!nested> -> !simulation.ref<!inner>
      simulation.ref.store %value to %field : !inner, !simulation.ref<!inner>
      %whole = simulation.ref.load %field : !simulation.ref<!inner> -> !inner
      simulation.return %whole : !inner
    }

    // CHECK-LABEL: simulation.func @escaped(
    // CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
    // CHECK: %[[FIELD:.*]] = simulation.ref.subelement %[[LOCAL]]
    // CHECK: simulation.call @consume(%arg0, %[[FIELD]])
    // CHECK: simulation.ref.load %[[FIELD]]
    // CHECK: simulation.return
    simulation.func @escaped(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %incoming: !array {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64, schedule.eval.borrowed_captures} {
      %local = simulation.ref.alloc %incoming : !array -> !simulation.ref<!array>
      %field = simulation.ref.subelement %local[[3]] : !simulation.ref<!array> -> !simulation.ref<i32>
      simulation.call @consume(%ctx, %field) : (!simulation.context, !simulation.ref<i32>) -> ()
      %value = simulation.ref.load %field : !simulation.ref<i32> -> i32
      simulation.return %value : i32
    }
    simulation.func private @consume(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %field: !simulation.ref<i32> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %one = arith.constant 1 : i32
      simulation.ref.store %one to %field : i32, !simulation.ref<i32>
      simulation.return
    }

    // Reference PHIs must not become choices between different local roots.
    // CHECK-LABEL: simulation.func @reference_choice(
    // CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
    // CHECK: simulation.ref.subelement %[[LOCAL]]
    // CHECK: simulation.ref.subelement %[[LOCAL]]
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.load
    // CHECK: simulation.return
    simulation.func @reference_choice(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %incoming: !array {simulation.capture_kind = 1 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64, schedule.eval.borrowed_captures} {
      %local = simulation.ref.alloc %incoming : !array -> !simulation.ref<!array>
      %a = simulation.ref.subelement %local[[0]] : !simulation.ref<!array> -> !simulation.ref<i32>
      %b = simulation.ref.subelement %local[[1]] : !simulation.ref<!array> -> !simulation.ref<i32>
      %published = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      cf.cond_br %choose, ^left, ^right
    ^left:
      %one = arith.constant 1 : i1
      simulation.ref.store %one to %published : i1, !simulation.ref<i1>
      cf.br ^read(%a : !simulation.ref<i32>)
    ^right:
      %zero = arith.constant 0 : i1
      simulation.ref.store %zero to %published : i1, !simulation.ref<i1>
      cf.br ^read(%b : !simulation.ref<i32>)
    ^read(%r: !simulation.ref<i32>):
      %value = simulation.ref.load %r : !simulation.ref<i32> -> i32
      simulation.return %value : i32
    }

    // A static outer view must not hide a dynamic descendant from the policy.
    // The child still needs checked declared-index accesses (LRM 7.4.5).
    // CHECK-LABEL: simulation.func @dynamic_child(
    // CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
    // CHECK: %[[CHILD:.*]] = simulation.ref.subelement %[[LOCAL]]
    // CHECK: %[[ELEMENT:.*]] = simulation.ref.array_element %[[CHILD]][%arg1]
    // CHECK: %[[VALUE:.*]] = simulation.ref.load %[[ELEMENT]]
    // CHECK: simulation.return %[[VALUE]]
    simulation.func @dynamic_child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i32 {simulation.capture_kind = 1 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64, schedule.eval.borrowed_captures} {
      %initial = simulation.aggregate.default : !nested
      %local = simulation.ref.alloc %initial : !nested -> !simulation.ref<!nested>
      %child = simulation.ref.subelement %local[[1]] : !simulation.ref<!nested> -> !simulation.ref<!inner>
      %element = simulation.ref.array_element %child[%index] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i32>
      %value = simulation.ref.load %element : !simulation.ref<i32> -> i32
      simulation.return %value : i32
    }
  }
}
