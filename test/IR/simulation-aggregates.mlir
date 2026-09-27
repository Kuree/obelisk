// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

!record = !simulation.unpacked_struct<[
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "flag", type = i1, ordinal = 1, packedOffset = 0>
]>
!words = !simulation.unpacked_array<3 : 1 x i8>
!packed_words = !simulation.packed_array<3 : 0 x i8>
!choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = false>
!tagged_choice = !simulation.packed_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>,
  #simulation.field<name = "nibble", type = i4, ordinal = 2, packedOffset = 0>
], isTagged = true, tagBits = 2>

module {
  simulation.design @aggregates {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.aggregates.roundtrip.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !record design
    simulation.net.decl 0 in 0 : !words design
    simulation.driver.decl 0 in 0 drives 0 : !words design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      simulation.return
    }

    simulation.func @roundtrip(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %record_ref: !simulation.ref<!record> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %driver: !simulation.driver<!words> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %index: i32 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %true = arith.constant true
      %default_record = simulation.aggregate.default : !record
      %record = simulation.aggregate.construct %zero, %true : (i8, i1) -> !record
      %byte = simulation.aggregate.extract %record[0] : (!record) -> i8
      %updated = simulation.aggregate.insert %one into %record[0] : (!record, i8) -> !record
      %array = simulation.aggregate.construct %zero, %one, %byte : (i8, i8, i8) -> !words
      %dynamic = simulation.array.extract_dynamic %array[%index] : (!words, i32) -> i8
      %union = simulation.union.construct %dynamic as 0 : (i8) -> !choice
      %union_byte = simulation.union.extract %union[0] : (!choice) -> i8
      %record_field = simulation.ref.subelement %record_ref[[0]] : !simulation.ref<!record> -> !simulation.ref<i8>
      simulation.ref.store %union_byte to %record_field : i8, !simulation.ref<i8>
      %array_default = simulation.aggregate.default : !words
      %array_ref = simulation.ref.alloc %array_default : !words -> !simulation.ref<!words>
      %array_field = simulation.ref.array_element %array_ref[%index] : (!simulation.ref<!words>, i32) -> !simulation.ref<i8>
      simulation.ref.store %byte to %array_field : i8, !simulation.ref<i8>
      %driver_field = simulation.driver.subelement %driver[[1]] : !simulation.driver<!words> -> !simulation.driver<i8>
      %driver_dynamic = simulation.driver.array_element %driver[%index] : (!simulation.driver<!words>, i32) -> !simulation.driver<i8>
      simulation.driver.drive %driver_field = %zero : !simulation.driver<i8>, i8
      simulation.driver.drive %driver_dynamic = %one : !simulation.driver<i8>, i8
      %packed_default = simulation.aggregate.default : !packed_words
      %bits = simulation.packed.flatten %packed_default : (!packed_words) -> i32
      %packed = simulation.packed.unflatten %bits : (i32) -> !packed_words
      %word = arith.constant 42 : i16
      %tagged = simulation.union.construct %word as 1 : (i16) -> !tagged_choice
      %tagged_bits = simulation.packed.flatten %tagged : (!tagged_choice) -> i18
      %tagged_copy = simulation.packed.unflatten %tagged_bits : (i18) -> !tagged_choice
      simulation.return
    }
  }
}

// CHECK: !simulation.unpacked_struct<[
// CHECK: !simulation.unpacked_array<3 : 1 x i8>
// CHECK: simulation.aggregate.default
// CHECK: simulation.aggregate.construct
// CHECK: simulation.aggregate.extract
// CHECK: simulation.aggregate.insert
// CHECK: simulation.array.extract_dynamic
// CHECK: simulation.union.construct
// CHECK: simulation.union.extract
// CHECK: simulation.ref.subelement
// CHECK: simulation.ref.array_element
// CHECK: simulation.driver.subelement
// CHECK: simulation.driver.array_element
// CHECK: simulation.packed.flatten
// CHECK: simulation.packed.unflatten
// CHECK: !simulation.packed_union<fields = {{.*}}isTagged = true, tagBits = 2>
// CHECK: simulation.packed.flatten {{.*}} -> i18
// CHECK: simulation.packed.unflatten {{.*}}(i18) -> !simulation.packed_union
