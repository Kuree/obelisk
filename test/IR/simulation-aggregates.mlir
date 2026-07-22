// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

!record = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "flag", type = i1, ordinal = 1, packedOffset = 0>
]>
!words = !obelisk_sim.unpacked_array<3 : 1 x i8>
!packed_words = !obelisk_sim.packed_array<3 : 0 x i8>
!choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = false>
!tagged_choice = !obelisk_sim.packed_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>,
  #obelisk_sim.field<name = "nibble", type = i4, ordinal = 2, packedOffset = 0>
], isTagged = true, tagBits = 2>

module {
  obelisk_sim.design @aggregates {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !record design
    obelisk_sim.net.decl 0 in 0 : !words design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !words design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @roundtrip(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %record_ref: !obelisk_sim.ref<!record> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %driver: !obelisk_sim.driver<!words> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %index: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %true = arith.constant true
      %default_record = obelisk_sim.aggregate.default : !record
      %record = obelisk_sim.aggregate.construct %zero, %true : (i8, i1) -> !record
      %byte = obelisk_sim.aggregate.extract %record[0] : (!record) -> i8
      %updated = obelisk_sim.aggregate.insert %one into %record[0] : (!record, i8) -> !record
      %array = obelisk_sim.aggregate.construct %zero, %one, %byte : (i8, i8, i8) -> !words
      %dynamic = obelisk_sim.array.extract_dynamic %array[%index] : (!words, i32) -> i8
      %union = obelisk_sim.union.construct %dynamic as 0 : (i8) -> !choice
      %union_byte = obelisk_sim.union.extract %union[0] : (!choice) -> i8
      %record_field = obelisk_sim.ref.subelement %record_ref[[0]] : !obelisk_sim.ref<!record> -> !obelisk_sim.ref<i8>
      obelisk_sim.ref.store %union_byte to %record_field : i8, !obelisk_sim.ref<i8>
      %array_default = obelisk_sim.aggregate.default : !words
      %array_ref = obelisk_sim.ref.alloc %array_default : !words -> !obelisk_sim.ref<!words>
      %array_field = obelisk_sim.ref.array_element %array_ref[%index] : (!obelisk_sim.ref<!words>, i32) -> !obelisk_sim.ref<i8>
      obelisk_sim.ref.store %byte to %array_field : i8, !obelisk_sim.ref<i8>
      %driver_field = obelisk_sim.driver.subelement %driver[[1]] : !obelisk_sim.driver<!words> -> !obelisk_sim.driver<i8>
      %driver_dynamic = obelisk_sim.driver.array_element %driver[%index] : (!obelisk_sim.driver<!words>, i32) -> !obelisk_sim.driver<i8>
      obelisk_sim.driver.drive %driver_field = %zero : !obelisk_sim.driver<i8>, i8
      obelisk_sim.driver.drive %driver_dynamic = %one : !obelisk_sim.driver<i8>, i8
      %packed_default = obelisk_sim.aggregate.default : !packed_words
      %bits = obelisk_sim.packed.flatten %packed_default : (!packed_words) -> i32
      %packed = obelisk_sim.packed.unflatten %bits : (i32) -> !packed_words
      %word = arith.constant 42 : i16
      %tagged = obelisk_sim.union.construct %word as 1 : (i16) -> !tagged_choice
      %tagged_bits = obelisk_sim.packed.flatten %tagged : (!tagged_choice) -> i18
      %tagged_copy = obelisk_sim.packed.unflatten %tagged_bits : (i18) -> !tagged_choice
      obelisk_sim.return
    }
  }
}

// CHECK: !obelisk_sim.unpacked_struct<[
// CHECK: !obelisk_sim.unpacked_array<3 : 1 x i8>
// CHECK: obelisk_sim.aggregate.default
// CHECK: obelisk_sim.aggregate.construct
// CHECK: obelisk_sim.aggregate.extract
// CHECK: obelisk_sim.aggregate.insert
// CHECK: obelisk_sim.array.extract_dynamic
// CHECK: obelisk_sim.union.construct
// CHECK: obelisk_sim.union.extract
// CHECK: obelisk_sim.ref.subelement
// CHECK: obelisk_sim.ref.array_element
// CHECK: obelisk_sim.driver.subelement
// CHECK: obelisk_sim.driver.array_element
// CHECK: obelisk_sim.packed.flatten
// CHECK: obelisk_sim.packed.unflatten
// CHECK: !obelisk_sim.packed_union<fields = {{.*}}isTagged = true, tagBits = 2>
// CHECK: obelisk_sim.packed.flatten {{.*}} -> i18
// CHECK: obelisk_sim.packed.unflatten {{.*}}(i18) -> !obelisk_sim.packed_union
