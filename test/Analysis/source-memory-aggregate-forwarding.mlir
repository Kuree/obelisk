// RUN: talos-opt %s --test-talos-source-memory-forwarding | FileCheck %s
!choice = !simulation.packed_union<fields = [#simulation.field<name = "bits", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "logic", type = !simulation.logic<8>, ordinal = 1, packedOffset = 0>], isTagged = false>
!pair = !simulation.packed_struct<[#simulation.field<name = "a", type = !choice, ordinal = 0, packedOffset = 8>, #simulation.field<name = "b", type = i8, ordinal = 1, packedOffset = 0>]>
!pairs = !simulation.unpacked_array<0 : 1 x !pair>
!bytes = !simulation.unpacked_array<0 : 1 x i8>
module {
  simulation.design @aggregate_forwarding {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !pair design
    simulation.storage.decl 1 in 0 : !pairs design
    simulation.storage.decl 2 in 0 : !bytes design
    simulation.storage.decl 3 in 0 : !bytes design
    simulation.storage.decl 4 in 0 : !bytes design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "fields"
    // IEEE 1800-2023 7.2, 7.3, 7.4.5: project a stored whole aggregate
    // through an exact dynamic parent. The physical store remains observable.
    // CHECK-LABEL: simulation.func @fields
    // CHECK: %[[VALUE:.*]] = simulation.ref.load
    // CHECK: simulation.ref.store %[[VALUE]] to
    // CHECK: %[[UNION:.*]] = simulation.aggregate.extract %[[VALUE]][0]
    // CHECK: %[[FIRST:.*]] = simulation.union.extract %[[UNION]][0]
    // CHECK: simulation.ref.store %[[FIRST]]
    // CHECK: %[[SECOND:.*]] = simulation.aggregate.extract %[[VALUE]][1]
    // CHECK: %[[RESPONSE:.*]] = simulation.ref.load
    // CHECK: arith.addi %[[SECOND]], %[[RESPONSE]]
    simulation.func @fields(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^entry
    ^entry:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!pair>
      %temporary = simulation.context.storage %ctx[1] : !simulation.ref<!pairs>
      %request = simulation.context.storage %ctx[2] : !simulation.ref<!bytes>
      %response = simulation.context.storage %ctx[3] : !simulation.ref<!bytes>
      %output = simulation.context.storage %ctx[4] : !simulation.ref<!bytes>
      %v = simulation.ref.load %input : !simulation.ref<!pair> -> !pair
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %two : i32
      cf.cond_br %more, ^body, ^wait
    ^body:
      %temp = simulation.ref.array_element %temporary[%i] : (!simulation.ref<!pairs>, i32) -> !simulation.ref<!pair>
      simulation.ref.store %v to %temp : !pair, !simulation.ref<!pair>
      %first = simulation.ref.subelement %temp[[0, 0]] : !simulation.ref<!pair> -> !simulation.ref<i8>
      %a = simulation.ref.load %first : !simulation.ref<i8> -> i8
      %req = simulation.ref.array_element %request[%i] : (!simulation.ref<!bytes>, i32) -> !simulation.ref<i8>
      simulation.ref.store %a to %req : i8, !simulation.ref<i8>
      %second = simulation.ref.subelement %temp[[1]] : !simulation.ref<!pair> -> !simulation.ref<i8>
      %b = simulation.ref.load %second : !simulation.ref<i8> -> i8
      %res = simulation.ref.array_element %response[%i] : (!simulation.ref<!bytes>, i32) -> !simulation.ref<i8>
      %r = simulation.ref.load %res : !simulation.ref<i8> -> i8
      %sum = arith.addi %b, %r : i8
      %out = simulation.ref.array_element %output[%i] : (!simulation.ref<!bytes>, i32) -> !simulation.ref<i8>
      simulation.ref.store %sum to %out : i8, !simulation.ref<i8>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^wait:
      simulation.suspend.any %input, %response edges [0, 0] to ^entry : !simulation.ref<!pair>, !simulation.ref<!bytes>
    }
  }
}
