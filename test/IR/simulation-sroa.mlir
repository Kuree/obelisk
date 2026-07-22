// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(canonicalize,sroa,canonicalize)))' | FileCheck %s

!pair = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "b", type = i16, ordinal = 1, packedOffset = 0>
]>
!inner = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "x", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "y", type = i16, ordinal = 1, packedOffset = 0>
]>
!outer = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "head", type = i32, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "inner", type = !inner, ordinal = 1, packedOffset = 0>
]>
!array64 = !obelisk_sim.unpacked_array<0 : 63 x i8>
!array65 = !obelisk_sim.unpacked_array<0 : 64 x i8>
!container = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "tag", type = i1, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "values", type = !array65, ordinal = 1, packedOffset = 0>
]>
!small_container = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "tag", type = i1, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "values", type = !array64, ordinal = 1, packedOffset = 0>
]>
!choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = false>
!tagged_choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = true>
!packed_pair = !obelisk_sim.packed_struct<[
  #obelisk_sim.field<name = "low", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "high", type = i16, ordinal = 1, packedOffset = 8>
]>
!packed_array = !obelisk_sim.packed_array<3 : 0 x i8>
!packed_container = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "tag", type = i1, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = !packed_array, ordinal = 1, packedOffset = 0>
]>
!packed_choice = !obelisk_sim.packed_union<fields = [
  #obelisk_sim.field<name = "first", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "second", type = i8, ordinal = 1, packedOffset = 0>
], isTagged = false>
!packed_tagged_choice = !obelisk_sim.packed_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = true, tagBits = 1>
!packed_tagged_logic_choice = !obelisk_sim.packed_union<fields = [
  #obelisk_sim.field<name = "byte", type = !obelisk_sim.logic<8>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = !obelisk_sim.logic<16>, ordinal = 1, packedOffset = 0>
], isTagged = true, tagBits = 1>
!aggregate_choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "record", type = !inner, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i32, ordinal = 1, packedOffset = 0>
], isTagged = false>

module {
  obelisk_sim.design @sroa {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.sroa.unused_field.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.sroa.recursive.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 function hierarchy "test.sroa.whole_copy.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 function hierarchy "test.sroa.packed_struct.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 function hierarchy "test.sroa.packed_array.9000005"
    obelisk_sim.code_unit.decl 9000006 in 0 function hierarchy "test.sroa.array_64.9000006"
    obelisk_sim.code_unit.decl 9000007 in 0 function hierarchy "test.sroa.array_65.9000007"
    obelisk_sim.code_unit.decl 9000008 in 0 function hierarchy "test.sroa.dynamic_blocks_array.9000008"
    obelisk_sim.code_unit.decl 9000009 in 0 function hierarchy "test.sroa.dynamic_value_blocks_array.9000009"
    obelisk_sim.code_unit.decl 9000010 in 0 function hierarchy "test.sroa.dynamic_value_safe_enclosing.9000010"
    obelisk_sim.code_unit.decl 9000011 in 0 function hierarchy "test.sroa.safe_enclosing_dynamic.9000011"
    obelisk_sim.code_unit.decl 9000012 in 0 function hierarchy "test.sroa.safe_enclosing_packed_extract.9000012"
    obelisk_sim.code_unit.decl 9000013 in 0 function hierarchy "test.sroa.safe_enclosing_packed_dynamic.9000013"
    obelisk_sim.code_unit.decl 9000014 in 0 function hierarchy "test.sroa.safe_enclosing_whole_large_array.9000014"
    obelisk_sim.code_unit.decl 9000015 in 0 function hierarchy "test.sroa.union_unique.9000015"
    obelisk_sim.code_unit.decl 9000016 in 0 function hierarchy "test.sroa.packed_union_unique.9000016"
    obelisk_sim.code_unit.decl 9000017 in 0 function hierarchy "test.sroa.union_recursive_field.9000017"
    obelisk_sim.code_unit.decl 9000018 in 0 function hierarchy "test.sroa.union_multiple_fields.9000018"
    obelisk_sim.code_unit.decl 9000019 in 0 function hierarchy "test.sroa.union_mismatched_initializer.9000019"
    obelisk_sim.code_unit.decl 9000020 in 0 function hierarchy "test.sroa.union_whole_use.9000020"
    obelisk_sim.code_unit.decl 9000021 in 0 function hierarchy "test.sroa.tagged_default.9000021"
    obelisk_sim.code_unit.decl 9000022 in 0 function hierarchy "test.sroa.packed_tagged_default.9000022"
    obelisk_sim.code_unit.decl 9000023 in 0 function hierarchy "test.sroa.packed_tagged_two_state_default.9000023"
    obelisk_sim.code_unit.decl 9000024 in 0 function hierarchy "test.sroa.escaping_reference.9000024"
    obelisk_sim.code_unit.decl 9000025 in 0 function hierarchy "test.sroa.consume.9000025"
    obelisk_sim.scope.decl 0

    // CHECK-LABEL: obelisk_sim.func @unused_field
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_struct
    // CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
    // CHECK: %[[ALLOC:.*]] = obelisk_sim.ref.alloc %[[ZERO]] : i8 -> !obelisk_sim.ref<i8>
    // CHECK: obelisk_sim.ref.store %arg1 to %[[ALLOC]]
    // CHECK: %[[VALUE:.*]] = obelisk_sim.ref.load %[[ALLOC]]
    // CHECK: obelisk_sim.return %[[VALUE]] : i8
    obelisk_sim.func @unused_field(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i8 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %default = obelisk_sim.aggregate.default : !pair
      %local = obelisk_sim.ref.alloc %default : !pair -> !obelisk_sim.ref<!pair>
      %field = obelisk_sim.ref.subelement %local[[0]] : !obelisk_sim.ref<!pair> -> !obelisk_sim.ref<i8>
      obelisk_sim.ref.store %value to %field : i8, !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // Recursive SROA creates only the leaf reached through the nested path.
    // CHECK-LABEL: obelisk_sim.func @recursive
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_struct
    // CHECK: obelisk_sim.ref.alloc {{.*}} : i8 -> !obelisk_sim.ref<i8>
    obelisk_sim.func @recursive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i8 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %default = obelisk_sim.aggregate.default : !outer
      %local = obelisk_sim.ref.alloc %default : !outer -> !obelisk_sim.ref<!outer>
      %leaf = obelisk_sim.ref.subelement %local[[1, 0]] : !obelisk_sim.ref<!outer> -> !obelisk_sim.ref<i8>
      obelisk_sim.ref.store %value to %leaf : i8, !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %leaf : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // Whole stores split and whole loads reconstruct aggregate values.
    // CHECK-LABEL: obelisk_sim.func @whole_copy
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_struct
    // CHECK: obelisk_sim.aggregate.extract %arg1[0]
    // CHECK: obelisk_sim.aggregate.extract %arg1[1]
    // CHECK: obelisk_sim.aggregate.construct
    obelisk_sim.func @whole_copy(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !pair {obelisk_sim.capture_kind = 2 : i32}) -> !pair
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %default = obelisk_sim.aggregate.default : !pair
      %local = obelisk_sim.ref.alloc %default : !pair -> !obelisk_sim.ref<!pair>
      obelisk_sim.ref.store %input to %local : !pair, !obelisk_sim.ref<!pair>
      %loaded = obelisk_sim.ref.load %local : !obelisk_sim.ref<!pair> -> !pair
      obelisk_sim.return %loaded : !pair
    }

    // Packed structs and fixed arrays use the same declaration-order SROA.
    // CHECK-LABEL: obelisk_sim.func @packed_struct
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.packed_struct
    // CHECK: obelisk_sim.ref.alloc {{.*}} : i16 -> !obelisk_sim.ref<i16>
    obelisk_sim.func @packed_struct(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i16
        attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %default = obelisk_sim.aggregate.default : !packed_pair
      %local = obelisk_sim.ref.alloc %default : !packed_pair -> !obelisk_sim.ref<!packed_pair>
      %field = obelisk_sim.ref.subelement %local[[1]] : !obelisk_sim.ref<!packed_pair> -> !obelisk_sim.ref<i16>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<i16> -> i16
      obelisk_sim.return %loaded : i16
    }

    // CHECK-LABEL: obelisk_sim.func @packed_array
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.packed_array
    // CHECK: obelisk_sim.ref.alloc {{.*}} : i8 -> !obelisk_sim.ref<i8>
    obelisk_sim.func @packed_array(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %default = obelisk_sim.aggregate.default : !packed_array
      %local = obelisk_sim.ref.alloc %default : !packed_array -> !obelisk_sim.ref<!packed_array>
      %field = obelisk_sim.ref.subelement %local[[2]] : !obelisk_sim.ref<!packed_array> -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // Arrays scalarize through 64 elements, but the 65-element boundary is
    // deliberately retained intact.
    // CHECK-LABEL: obelisk_sim.func @array_64
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_array
    // CHECK: obelisk_sim.ref.alloc {{.*}} : i8 -> !obelisk_sim.ref<i8>
    obelisk_sim.func @array_64(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %default = obelisk_sim.aggregate.default : !array64
      %local = obelisk_sim.ref.alloc %default : !array64 -> !obelisk_sim.ref<!array64>
      %element = obelisk_sim.ref.subelement %local[[63]] : !obelisk_sim.ref<!array64> -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %element : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // CHECK-LABEL: obelisk_sim.func @array_65
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_array<0 : 64 x i8> -> !obelisk_sim.ref<!obelisk_sim.unpacked_array<0 : 64 x i8>>
    // CHECK: obelisk_sim.ref.subelement
    obelisk_sim.func @array_65(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000007 : i64} {
      %default = obelisk_sim.aggregate.default : !array65
      %local = obelisk_sim.ref.alloc %default : !array65 -> !obelisk_sim.ref<!array65>
      %element = obelisk_sim.ref.subelement %local[[64]] : !obelisk_sim.ref<!array65> -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %element : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // CHECK-LABEL: obelisk_sim.func @dynamic_blocks_array
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_array<0 : 63 x i8> -> !obelisk_sim.ref<!obelisk_sim.unpacked_array<0 : 63 x i8>>
    // CHECK: obelisk_sim.ref.array_element
    obelisk_sim.func @dynamic_blocks_array(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %index: i64 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000008 : i64} {
      %default = obelisk_sim.aggregate.default : !array64
      %local = obelisk_sim.ref.alloc %default : !array64 -> !obelisk_sim.ref<!array64>
      %element = obelisk_sim.ref.array_element %local[%index] : (!obelisk_sim.ref<!array64>, i64) -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %element : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // A pure dynamic read of a loaded value also blocks that array's SROA.
    // CHECK-LABEL: obelisk_sim.func @dynamic_value_blocks_array
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_array<0 : 63 x i8> -> !obelisk_sim.ref<!obelisk_sim.unpacked_array<0 : 63 x i8>>
    // CHECK: obelisk_sim.ref.load {{.*}} -> !obelisk_sim.unpacked_array<0 : 63 x i8>
    // CHECK: obelisk_sim.array.extract_dynamic
    obelisk_sim.func @dynamic_value_blocks_array(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %index: i64 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000009 : i64} {
      %default = obelisk_sim.aggregate.default : !array64
      %local = obelisk_sim.ref.alloc %default : !array64 -> !obelisk_sim.ref<!array64>
      %whole = obelisk_sim.ref.load %local : !obelisk_sim.ref<!array64> -> !array64
      %element = obelisk_sim.array.extract_dynamic %whole[%index] : (!array64, i64) -> i8
      obelisk_sim.return %element : i8
    }

    // The dynamic value read retains only the nested array, not its container.
    // CHECK-LABEL: obelisk_sim.func @dynamic_value_safe_enclosing
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_struct
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_array<0 : 63 x i8> -> !obelisk_sim.ref<!obelisk_sim.unpacked_array<0 : 63 x i8>>
    // CHECK: obelisk_sim.array.extract_dynamic
    obelisk_sim.func @dynamic_value_safe_enclosing(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %index: i64 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000010 : i64} {
      %default = obelisk_sim.aggregate.default : !small_container
      %local = obelisk_sim.ref.alloc %default : !small_container -> !obelisk_sim.ref<!small_container>
      %array = obelisk_sim.ref.subelement %local[[1]] : !obelisk_sim.ref<!small_container> -> !obelisk_sim.ref<!array64>
      %whole = obelisk_sim.ref.load %array : !obelisk_sim.ref<!array64> -> !array64
      %element = obelisk_sim.array.extract_dynamic %whole[%index] : (!array64, i64) -> i8
      obelisk_sim.return %element : i8
    }

    // A dynamic view blocks decomposition of its array. The safe-access chain
    // still lets SROA split away the enclosing struct.
    // CHECK-LABEL: obelisk_sim.func @safe_enclosing_dynamic
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_struct
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_array<0 : 64 x i8> -> !obelisk_sim.ref<!obelisk_sim.unpacked_array<0 : 64 x i8>>
    // CHECK: obelisk_sim.ref.array_element
    obelisk_sim.func @safe_enclosing_dynamic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %index: i64 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000011 : i64} {
      %default = obelisk_sim.aggregate.default : !container
      %local = obelisk_sim.ref.alloc %default : !container -> !obelisk_sim.ref<!container>
      %array = obelisk_sim.ref.subelement %local[[1]] : !obelisk_sim.ref<!container> -> !obelisk_sim.ref<!array65>
      %element = obelisk_sim.ref.array_element %array[%index] : (!obelisk_sim.ref<!array65>, i64) -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %element : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // Static and dynamic packed views safely retain their nested packed array
    // while still permitting the outer struct to decompose.
    // CHECK-LABEL: obelisk_sim.func @safe_enclosing_packed_extract
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_struct
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.packed_array<3 : 0 x i8> -> !obelisk_sim.ref<!obelisk_sim.packed_array<3 : 0 x i8>>
    // CHECK: obelisk_sim.ref.extract
    obelisk_sim.func @safe_enclosing_packed_extract(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000012 : i64} {
      %default = obelisk_sim.aggregate.default : !packed_container
      %local = obelisk_sim.ref.alloc %default : !packed_container -> !obelisk_sim.ref<!packed_container>
      %packed = obelisk_sim.ref.subelement %local[[1]] : !obelisk_sim.ref<!packed_container> -> !obelisk_sim.ref<!packed_array>
      %byte = obelisk_sim.ref.extract %packed from 0 : !obelisk_sim.ref<!packed_array> -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %byte : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // CHECK-LABEL: obelisk_sim.func @safe_enclosing_packed_dynamic
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_struct
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.packed_array<3 : 0 x i8> -> !obelisk_sim.ref<!obelisk_sim.packed_array<3 : 0 x i8>>
    // CHECK: obelisk_sim.ref.dyn_extract
    obelisk_sim.func @safe_enclosing_packed_dynamic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %index: i64 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000013 : i64} {
      %default = obelisk_sim.aggregate.default : !packed_container
      %local = obelisk_sim.ref.alloc %default : !packed_container -> !obelisk_sim.ref<!packed_container>
      %packed = obelisk_sim.ref.subelement %local[[1]] : !obelisk_sim.ref<!packed_container> -> !obelisk_sim.ref<!packed_array>
      %byte = obelisk_sim.ref.dyn_extract %packed from %index : (!obelisk_sim.ref<!packed_array>, i64) -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %byte : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // Whole access to a non-destructurable nested array also remains safe for
    // decomposition of the enclosing struct.
    // CHECK-LABEL: obelisk_sim.func @safe_enclosing_whole_large_array
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_struct
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_array<0 : 64 x i8> -> !obelisk_sim.ref<!obelisk_sim.unpacked_array<0 : 64 x i8>>
    obelisk_sim.func @safe_enclosing_whole_large_array(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> !array65
        attributes {entry_kind = 8 : i32, code_unit_id = 9000014 : i64} {
      %default = obelisk_sim.aggregate.default : !container
      %local = obelisk_sim.ref.alloc %default : !container -> !obelisk_sim.ref<!container>
      %array = obelisk_sim.ref.subelement %local[[1]] : !obelisk_sim.ref<!container> -> !obelisk_sim.ref<!array65>
      %whole = obelisk_sim.ref.load %array : !obelisk_sim.ref<!array65> -> !array65
      obelisk_sim.return %whole : !array65
    }

    // A union is scalarized only when the initializer and every view agree.
    // CHECK-LABEL: obelisk_sim.func @union_unique
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_union
    // CHECK: obelisk_sim.ref.alloc %arg1 : i8 -> !obelisk_sim.ref<i8>
    obelisk_sim.func @union_unique(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i8 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000015 : i64} {
      %initial = obelisk_sim.union.construct %value as 0 : (i8) -> !choice
      %local = obelisk_sim.ref.alloc %initial : !choice -> !obelisk_sim.ref<!choice>
      %field = obelisk_sim.ref.subelement %local[[0]] : !obelisk_sim.ref<!choice> -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // Packed unions use the same guarded unique-field scalarization.
    // CHECK-LABEL: obelisk_sim.func @packed_union_unique
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.packed_union
    // CHECK: obelisk_sim.ref.alloc %arg1 : i8 -> !obelisk_sim.ref<i8>
    obelisk_sim.func @packed_union_unique(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i8 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000016 : i64} {
      %initial = obelisk_sim.union.construct %value as 1 : (i8) -> !packed_choice
      %local = obelisk_sim.ref.alloc %initial : !packed_choice -> !obelisk_sim.ref<!packed_choice>
      %field = obelisk_sim.ref.subelement %local[[1]] : !obelisk_sim.ref<!packed_choice> -> !obelisk_sim.ref<i8>
      obelisk_sim.ref.store %value to %field : i8, !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // A selected aggregate field is recursively scalarized.
    // CHECK-LABEL: obelisk_sim.func @union_recursive_field
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_union
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.unpacked_struct
    // CHECK: obelisk_sim.ref.alloc {{.*}} : i8 -> !obelisk_sim.ref<i8>
    obelisk_sim.func @union_recursive_field(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !inner {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000017 : i64} {
      %initial = obelisk_sim.union.construct %value as 0 : (!inner) -> !aggregate_choice
      %local = obelisk_sim.ref.alloc %initial : !aggregate_choice -> !obelisk_sim.ref<!aggregate_choice>
      %leaf = obelisk_sim.ref.subelement %local[[0, 0]] : !obelisk_sim.ref<!aggregate_choice> -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %leaf : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // CHECK-LABEL: obelisk_sim.func @union_multiple_fields
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_union
    obelisk_sim.func @union_multiple_fields(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %byte: i8 {obelisk_sim.capture_kind = 2 : i32},
        %word: i16 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000018 : i64} {
      %initial = obelisk_sim.union.construct %byte as 0 : (i8) -> !choice
      %local = obelisk_sim.ref.alloc %initial : !choice -> !obelisk_sim.ref<!choice>
      %first = obelisk_sim.ref.subelement %local[[0]] : !obelisk_sim.ref<!choice> -> !obelisk_sim.ref<i8>
      %second = obelisk_sim.ref.subelement %local[[1]] : !obelisk_sim.ref<!choice> -> !obelisk_sim.ref<i16>
      obelisk_sim.ref.store %word to %second : i16, !obelisk_sim.ref<i16>
      %loaded = obelisk_sim.ref.load %first : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // CHECK-LABEL: obelisk_sim.func @union_mismatched_initializer
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_union
    obelisk_sim.func @union_mismatched_initializer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %byte: i8 {obelisk_sim.capture_kind = 2 : i32}) -> i16
        attributes {entry_kind = 8 : i32, code_unit_id = 9000019 : i64} {
      %initial = obelisk_sim.union.construct %byte as 0 : (i8) -> !choice
      %local = obelisk_sim.ref.alloc %initial : !choice -> !obelisk_sim.ref<!choice>
      %field = obelisk_sim.ref.subelement %local[[1]] : !obelisk_sim.ref<!choice> -> !obelisk_sim.ref<i16>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<i16> -> i16
      obelisk_sim.return %loaded : i16
    }

    // CHECK-LABEL: obelisk_sim.func @union_whole_use
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_union
    // CHECK: obelisk_sim.ref.load {{.*}} -> !obelisk_sim.unpacked_union
    obelisk_sim.func @union_whole_use(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i8 {obelisk_sim.capture_kind = 2 : i32}) -> !choice
        attributes {entry_kind = 8 : i32, code_unit_id = 9000020 : i64} {
      %initial = obelisk_sim.union.construct %value as 0 : (i8) -> !choice
      %local = obelisk_sim.ref.alloc %initial : !choice -> !obelisk_sim.ref<!choice>
      %loaded = obelisk_sim.ref.load %local : !obelisk_sim.ref<!choice> -> !choice
      obelisk_sim.return %loaded : !choice
    }

    // Tagged-union defaults have no selected field and retain shared backing.
    // CHECK-LABEL: obelisk_sim.func @tagged_default
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_union
    // CHECK: obelisk_sim.ref.subelement
    obelisk_sim.func @tagged_default(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000021 : i64} {
      %default = obelisk_sim.aggregate.default : !tagged_choice
      %local = obelisk_sim.ref.alloc %default : !tagged_choice -> !obelisk_sim.ref<!tagged_choice>
      %field = obelisk_sim.ref.subelement %local[[0]] : !obelisk_sim.ref<!tagged_choice> -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // A four-state packed tagged default has an unknown tag and retains backing.
    // CHECK-LABEL: obelisk_sim.func @packed_tagged_default
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.packed_union
    // CHECK: obelisk_sim.ref.subelement
    obelisk_sim.func @packed_tagged_default(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000022 : i64} {
      %default = obelisk_sim.aggregate.default : !packed_tagged_logic_choice
      %local = obelisk_sim.ref.alloc %default : !packed_tagged_logic_choice -> !obelisk_sim.ref<!packed_tagged_logic_choice>
      %field = obelisk_sim.ref.subelement %local[[0]] : !obelisk_sim.ref<!packed_tagged_logic_choice> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %loaded : !obelisk_sim.logic<8>
    }

    // A two-state packed tagged default has tag zero, selecting field zero.
    // CHECK-LABEL: obelisk_sim.func @packed_tagged_two_state_default
    // CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.packed_union
    // CHECK: obelisk_sim.ref.alloc {{.*}} : i8 -> !obelisk_sim.ref<i8>
    obelisk_sim.func @packed_tagged_two_state_default(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000023 : i64} {
      %default = obelisk_sim.aggregate.default : !packed_tagged_choice
      %local = obelisk_sim.ref.alloc %default : !packed_tagged_choice -> !obelisk_sim.ref<!packed_tagged_choice>
      %field = obelisk_sim.ref.subelement %local[[0]] : !obelisk_sim.ref<!packed_tagged_choice> -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }

    // An escaping reference is a blocking use and retains shared backing.
    // CHECK-LABEL: obelisk_sim.func @escaping_reference
    // CHECK: obelisk_sim.ref.alloc {{.*}} : !obelisk_sim.unpacked_struct
    // CHECK: obelisk_sim.call @consume
    obelisk_sim.func @escaping_reference(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000024 : i64} {
      %default = obelisk_sim.aggregate.default : !pair
      %local = obelisk_sim.ref.alloc %default : !pair -> !obelisk_sim.ref<!pair>
      %result = obelisk_sim.call @consume(%ctx, %local) : (!obelisk_sim.context, !obelisk_sim.ref<!pair>) -> i8
      obelisk_sim.return %result : i8
    }

    obelisk_sim.func private @consume(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!pair> {obelisk_sim.capture_kind = 1 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000025 : i64} {
      %field = obelisk_sim.ref.subelement %value[[0]] : !obelisk_sim.ref<!pair> -> !obelisk_sim.ref<i8>
      %loaded = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %loaded : i8
    }
  }
}
