// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(canonicalize,sroa,canonicalize)))' | FileCheck %s

!pair = !simulation.unpacked_struct<[
  #simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "b", type = i16, ordinal = 1, packedOffset = 0>
]>
!inner = !simulation.unpacked_struct<[
  #simulation.field<name = "x", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "y", type = i16, ordinal = 1, packedOffset = 0>
]>
!outer = !simulation.unpacked_struct<[
  #simulation.field<name = "head", type = i32, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "inner", type = !inner, ordinal = 1, packedOffset = 0>
]>
!array64 = !simulation.unpacked_array<0 : 63 x i8>
!array65 = !simulation.unpacked_array<0 : 64 x i8>
!container = !simulation.unpacked_struct<[
  #simulation.field<name = "tag", type = i1, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "values", type = !array65, ordinal = 1, packedOffset = 0>
]>
!small_container = !simulation.unpacked_struct<[
  #simulation.field<name = "tag", type = i1, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "values", type = !array64, ordinal = 1, packedOffset = 0>
]>
!choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = false>
!tagged_choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = true>
!packed_pair = !simulation.packed_struct<[
  #simulation.field<name = "low", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "high", type = i16, ordinal = 1, packedOffset = 8>
]>
!packed_array = !simulation.packed_array<3 : 0 x i8>
!packed_container = !simulation.unpacked_struct<[
  #simulation.field<name = "tag", type = i1, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = !packed_array, ordinal = 1, packedOffset = 0>
]>
!packed_choice = !simulation.packed_union<fields = [
  #simulation.field<name = "first", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "second", type = i8, ordinal = 1, packedOffset = 0>
], isTagged = false>
!packed_tagged_choice = !simulation.packed_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = true, tagBits = 1>
!packed_tagged_logic_choice = !simulation.packed_union<fields = [
  #simulation.field<name = "byte", type = !simulation.logic<8>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = !simulation.logic<16>, ordinal = 1, packedOffset = 0>
], isTagged = true, tagBits = 1>
!aggregate_choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "record", type = !inner, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i32, ordinal = 1, packedOffset = 0>
], isTagged = false>

module {
  simulation.design @sroa {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.sroa.unused_field.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.sroa.recursive.9000002"
    simulation.code_unit.decl 9000003 in 0 function hierarchy "test.sroa.whole_copy.9000003"
    simulation.code_unit.decl 9000004 in 0 function hierarchy "test.sroa.packed_struct.9000004"
    simulation.code_unit.decl 9000005 in 0 function hierarchy "test.sroa.packed_array.9000005"
    simulation.code_unit.decl 9000006 in 0 function hierarchy "test.sroa.array_64.9000006"
    simulation.code_unit.decl 9000007 in 0 function hierarchy "test.sroa.array_65.9000007"
    simulation.code_unit.decl 9000008 in 0 function hierarchy "test.sroa.dynamic_blocks_array.9000008"
    simulation.code_unit.decl 9000009 in 0 function hierarchy "test.sroa.dynamic_value_blocks_array.9000009"
    simulation.code_unit.decl 9000010 in 0 function hierarchy "test.sroa.dynamic_value_safe_enclosing.9000010"
    simulation.code_unit.decl 9000011 in 0 function hierarchy "test.sroa.safe_enclosing_dynamic.9000011"
    simulation.code_unit.decl 9000012 in 0 function hierarchy "test.sroa.safe_enclosing_packed_extract.9000012"
    simulation.code_unit.decl 9000013 in 0 function hierarchy "test.sroa.safe_enclosing_packed_dynamic.9000013"
    simulation.code_unit.decl 9000014 in 0 function hierarchy "test.sroa.safe_enclosing_whole_large_array.9000014"
    simulation.code_unit.decl 9000015 in 0 function hierarchy "test.sroa.union_unique.9000015"
    simulation.code_unit.decl 9000016 in 0 function hierarchy "test.sroa.packed_union_unique.9000016"
    simulation.code_unit.decl 9000017 in 0 function hierarchy "test.sroa.union_recursive_field.9000017"
    simulation.code_unit.decl 9000018 in 0 function hierarchy "test.sroa.union_multiple_fields.9000018"
    simulation.code_unit.decl 9000019 in 0 function hierarchy "test.sroa.union_mismatched_initializer.9000019"
    simulation.code_unit.decl 9000020 in 0 function hierarchy "test.sroa.union_whole_use.9000020"
    simulation.code_unit.decl 9000021 in 0 function hierarchy "test.sroa.tagged_default.9000021"
    simulation.code_unit.decl 9000022 in 0 function hierarchy "test.sroa.packed_tagged_default.9000022"
    simulation.code_unit.decl 9000023 in 0 function hierarchy "test.sroa.packed_tagged_two_state_default.9000023"
    simulation.code_unit.decl 9000024 in 0 function hierarchy "test.sroa.escaping_reference.9000024"
    simulation.code_unit.decl 9000025 in 0 function hierarchy "test.sroa.consume.9000025"
    simulation.scope.decl 0

    // CHECK-LABEL: simulation.func @unused_field
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_struct
    // CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
    // CHECK: %[[ALLOC:.*]] = simulation.ref.alloc %[[ZERO]] : i8 -> !simulation.ref<i8>
    // CHECK: simulation.ref.store %arg1 to %[[ALLOC]]
    // CHECK: %[[VALUE:.*]] = simulation.ref.load %[[ALLOC]]
    // CHECK: simulation.return %[[VALUE]] : i8
    simulation.func @unused_field(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %default = simulation.aggregate.default : !pair
      %local = simulation.ref.alloc %default : !pair -> !simulation.ref<!pair>
      %field = simulation.ref.subelement %local[[0]] : !simulation.ref<!pair> -> !simulation.ref<i8>
      simulation.ref.store %value to %field : i8, !simulation.ref<i8>
      %loaded = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // Recursive SROA creates only the leaf reached through the nested path.
    // CHECK-LABEL: simulation.func @recursive
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_struct
    // CHECK: simulation.ref.alloc {{.*}} : i8 -> !simulation.ref<i8>
    simulation.func @recursive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %default = simulation.aggregate.default : !outer
      %local = simulation.ref.alloc %default : !outer -> !simulation.ref<!outer>
      %leaf = simulation.ref.subelement %local[[1, 0]] : !simulation.ref<!outer> -> !simulation.ref<i8>
      simulation.ref.store %value to %leaf : i8, !simulation.ref<i8>
      %loaded = simulation.ref.load %leaf : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // Whole stores split and whole loads reconstruct aggregate values.
    // CHECK-LABEL: simulation.func @whole_copy
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_struct
    // CHECK: simulation.aggregate.extract %arg1[0]
    // CHECK: simulation.aggregate.extract %arg1[1]
    // CHECK: simulation.aggregate.construct
    simulation.func @whole_copy(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !pair {simulation.capture_kind = 2 : i32}) -> !pair
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %default = simulation.aggregate.default : !pair
      %local = simulation.ref.alloc %default : !pair -> !simulation.ref<!pair>
      simulation.ref.store %input to %local : !pair, !simulation.ref<!pair>
      %loaded = simulation.ref.load %local : !simulation.ref<!pair> -> !pair
      simulation.return %loaded : !pair
    }

    // Packed structs and fixed arrays use the same declaration-order SROA.
    // CHECK-LABEL: simulation.func @packed_struct
    // CHECK-NOT: !simulation.ref<!simulation.packed_struct
    // CHECK: simulation.ref.alloc {{.*}} : i16 -> !simulation.ref<i16>
    simulation.func @packed_struct(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i16
        attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %default = simulation.aggregate.default : !packed_pair
      %local = simulation.ref.alloc %default : !packed_pair -> !simulation.ref<!packed_pair>
      %field = simulation.ref.subelement %local[[1]] : !simulation.ref<!packed_pair> -> !simulation.ref<i16>
      %loaded = simulation.ref.load %field : !simulation.ref<i16> -> i16
      simulation.return %loaded : i16
    }

    // CHECK-LABEL: simulation.func @packed_array
    // CHECK-NOT: !simulation.ref<!simulation.packed_array
    // CHECK: simulation.ref.alloc {{.*}} : i8 -> !simulation.ref<i8>
    simulation.func @packed_array(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %default = simulation.aggregate.default : !packed_array
      %local = simulation.ref.alloc %default : !packed_array -> !simulation.ref<!packed_array>
      %field = simulation.ref.subelement %local[[2]] : !simulation.ref<!packed_array> -> !simulation.ref<i8>
      %loaded = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // Arrays scalarize through 64 elements, but the 65-element boundary is
    // deliberately retained intact.
    // CHECK-LABEL: simulation.func @array_64
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_array
    // CHECK: simulation.ref.alloc {{.*}} : i8 -> !simulation.ref<i8>
    simulation.func @array_64(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %default = simulation.aggregate.default : !array64
      %local = simulation.ref.alloc %default : !array64 -> !simulation.ref<!array64>
      %element = simulation.ref.subelement %local[[63]] : !simulation.ref<!array64> -> !simulation.ref<i8>
      %loaded = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // CHECK-LABEL: simulation.func @array_65
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_array<0 : 64 x i8> -> !simulation.ref<!simulation.unpacked_array<0 : 64 x i8>>
    // CHECK: simulation.aggregate.extract
    simulation.func @array_65(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000007 : i64} {
      %default = simulation.aggregate.default : !array65
      %local = simulation.ref.alloc %default : !array65 -> !simulation.ref<!array65>
      %element = simulation.ref.subelement %local[[64]] : !simulation.ref<!array65> -> !simulation.ref<i8>
      %loaded = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // CHECK-LABEL: simulation.func @dynamic_blocks_array
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_array<0 : 63 x i8> -> !simulation.ref<!simulation.unpacked_array<0 : 63 x i8>>
    // CHECK: simulation.array.extract_dynamic
    simulation.func @dynamic_blocks_array(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000008 : i64} {
      %default = simulation.aggregate.default : !array64
      %local = simulation.ref.alloc %default : !array64 -> !simulation.ref<!array64>
      %element = simulation.ref.array_element %local[%index] : (!simulation.ref<!array64>, i64) -> !simulation.ref<i8>
      %loaded = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // A pure dynamic read of a loaded value also blocks that array's SROA.
    // Canonicalization keeps the automatic value exposed to later mem2reg
    // (LRM 6.21, 7.4.5); SROA cannot destructure the dynamic selection.
    // CHECK-LABEL: simulation.func @dynamic_value_blocks_array
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_array<0 : 63 x i8> -> !simulation.ref<!simulation.unpacked_array<0 : 63 x i8>>
    // CHECK: %[[WHOLE:.*]] = simulation.ref.load
    // CHECK: simulation.array.extract_dynamic %[[WHOLE]]
    simulation.func @dynamic_value_blocks_array(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000009 : i64} {
      %default = simulation.aggregate.default : !array64
      %local = simulation.ref.alloc %default : !array64 -> !simulation.ref<!array64>
      %whole = simulation.ref.load %local : !simulation.ref<!array64> -> !array64
      %element = simulation.array.extract_dynamic %whole[%index] : (!array64, i64) -> i8
      simulation.return %element : i8
    }

    // The whole value read allows the small container to scalarize while
    // preserving the dynamic value selection (LRM 7.4.5).
    // CHECK-LABEL: simulation.func @dynamic_value_safe_enclosing
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_struct
    // CHECK: simulation.ref.alloc {{.*}} : i8 -> !simulation.ref<i8>
    // CHECK: simulation.array.extract_dynamic
    simulation.func @dynamic_value_safe_enclosing(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000010 : i64} {
      %default = simulation.aggregate.default : !small_container
      %local = simulation.ref.alloc %default : !small_container -> !simulation.ref<!small_container>
      %array = simulation.ref.subelement %local[[1]] : !simulation.ref<!small_container> -> !simulation.ref<!array64>
      %whole = simulation.ref.load %array : !simulation.ref<!array64> -> !array64
      %element = simulation.array.extract_dynamic %whole[%index] : (!array64, i64) -> i8
      simulation.return %element : i8
    }

    // A dynamic view blocks decomposition of its array. The safe-access chain
    // still lets SROA split away the enclosing struct.
    // CHECK-LABEL: simulation.func @safe_enclosing_dynamic
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_struct
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_array<0 : 64 x i8> -> !simulation.ref<!simulation.unpacked_array<0 : 64 x i8>>
    // CHECK: simulation.array.extract_dynamic
    simulation.func @safe_enclosing_dynamic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000011 : i64} {
      %default = simulation.aggregate.default : !container
      %local = simulation.ref.alloc %default : !container -> !simulation.ref<!container>
      %array = simulation.ref.subelement %local[[1]] : !simulation.ref<!container> -> !simulation.ref<!array65>
      %element = simulation.ref.array_element %array[%index] : (!simulation.ref<!array65>, i64) -> !simulation.ref<i8>
      %loaded = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // Static and dynamic packed views safely retain their nested packed array
    // while still permitting the outer struct to decompose.
    // CHECK-LABEL: simulation.func @safe_enclosing_packed_extract
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_struct
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.packed_array<3 : 0 x i8> -> !simulation.ref<!simulation.packed_array<3 : 0 x i8>>
    // CHECK: simulation.bits.dyn_extract
    simulation.func @safe_enclosing_packed_extract(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000012 : i64} {
      %default = simulation.aggregate.default : !packed_container
      %local = simulation.ref.alloc %default : !packed_container -> !simulation.ref<!packed_container>
      %packed = simulation.ref.subelement %local[[1]] : !simulation.ref<!packed_container> -> !simulation.ref<!packed_array>
      %byte = simulation.ref.extract %packed from 0 : !simulation.ref<!packed_array> -> !simulation.ref<i8>
      %loaded = simulation.ref.load %byte : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // CHECK-LABEL: simulation.func @safe_enclosing_packed_dynamic
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_struct
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.packed_array<3 : 0 x i8> -> !simulation.ref<!simulation.packed_array<3 : 0 x i8>>
    // CHECK: simulation.bits.dyn_extract
    simulation.func @safe_enclosing_packed_dynamic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000013 : i64} {
      %default = simulation.aggregate.default : !packed_container
      %local = simulation.ref.alloc %default : !packed_container -> !simulation.ref<!packed_container>
      %packed = simulation.ref.subelement %local[[1]] : !simulation.ref<!packed_container> -> !simulation.ref<!packed_array>
      %byte = simulation.ref.dyn_extract %packed from %index : (!simulation.ref<!packed_array>, i64) -> !simulation.ref<i8>
      %loaded = simulation.ref.load %byte : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // Whole access to a non-destructurable nested array also remains safe for
    // decomposition of the enclosing struct.
    // CHECK-LABEL: simulation.func @safe_enclosing_whole_large_array
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_struct
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_array<0 : 64 x i8> -> !simulation.ref<!simulation.unpacked_array<0 : 64 x i8>>
    simulation.func @safe_enclosing_whole_large_array(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !array65
        attributes {entry_kind = 8 : i32, code_unit_id = 9000014 : i64} {
      %default = simulation.aggregate.default : !container
      %local = simulation.ref.alloc %default : !container -> !simulation.ref<!container>
      %array = simulation.ref.subelement %local[[1]] : !simulation.ref<!container> -> !simulation.ref<!array65>
      %whole = simulation.ref.load %array : !simulation.ref<!array65> -> !array65
      simulation.return %whole : !array65
    }

    // A union is scalarized only when the initializer and every view agree.
    // CHECK-LABEL: simulation.func @union_unique
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_union
    // CHECK: simulation.ref.alloc %arg1 : i8 -> !simulation.ref<i8>
    simulation.func @union_unique(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000015 : i64} {
      %initial = simulation.union.construct %value as 0 : (i8) -> !choice
      %local = simulation.ref.alloc %initial : !choice -> !simulation.ref<!choice>
      %field = simulation.ref.subelement %local[[0]] : !simulation.ref<!choice> -> !simulation.ref<i8>
      %loaded = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // Packed unions use the same guarded unique-field scalarization.
    // CHECK-LABEL: simulation.func @packed_union_unique
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.packed_union
    // CHECK: simulation.packed.unflatten %arg1
    // CHECK: simulation.ref.store
    // CHECK: simulation.packed.flatten
    simulation.func @packed_union_unique(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000016 : i64} {
      %initial = simulation.union.construct %value as 1 : (i8) -> !packed_choice
      %local = simulation.ref.alloc %initial : !packed_choice -> !simulation.ref<!packed_choice>
      %field = simulation.ref.subelement %local[[1]] : !simulation.ref<!packed_choice> -> !simulation.ref<i8>
      simulation.ref.store %value to %field : i8, !simulation.ref<i8>
      %loaded = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // A selected aggregate field is recursively scalarized.
    // CHECK-LABEL: simulation.func @union_recursive_field
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_union
    // CHECK-NOT: !simulation.ref<!simulation.unpacked_struct
    // CHECK: simulation.ref.alloc {{.*}} : i8 -> !simulation.ref<i8>
    simulation.func @union_recursive_field(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !inner {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000017 : i64} {
      %initial = simulation.union.construct %value as 0 : (!inner) -> !aggregate_choice
      %local = simulation.ref.alloc %initial : !aggregate_choice -> !simulation.ref<!aggregate_choice>
      %leaf = simulation.ref.subelement %local[[0, 0]] : !simulation.ref<!aggregate_choice> -> !simulation.ref<i8>
      %loaded = simulation.ref.load %leaf : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // CHECK-LABEL: simulation.func @union_multiple_fields
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_union
    simulation.func @union_multiple_fields(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %byte: i8 {simulation.capture_kind = 2 : i32},
        %word: i16 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000018 : i64} {
      %initial = simulation.union.construct %byte as 0 : (i8) -> !choice
      %local = simulation.ref.alloc %initial : !choice -> !simulation.ref<!choice>
      %first = simulation.ref.subelement %local[[0]] : !simulation.ref<!choice> -> !simulation.ref<i8>
      %second = simulation.ref.subelement %local[[1]] : !simulation.ref<!choice> -> !simulation.ref<i16>
      simulation.ref.store %word to %second : i16, !simulation.ref<i16>
      %loaded = simulation.ref.load %first : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // CHECK-LABEL: simulation.func @union_mismatched_initializer
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_union
    simulation.func @union_mismatched_initializer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %byte: i8 {simulation.capture_kind = 2 : i32}) -> i16
        attributes {entry_kind = 8 : i32, code_unit_id = 9000019 : i64} {
      %initial = simulation.union.construct %byte as 0 : (i8) -> !choice
      %local = simulation.ref.alloc %initial : !choice -> !simulation.ref<!choice>
      %field = simulation.ref.subelement %local[[1]] : !simulation.ref<!choice> -> !simulation.ref<i16>
      %loaded = simulation.ref.load %field : !simulation.ref<i16> -> i16
      simulation.return %loaded : i16
    }

    // CHECK-LABEL: simulation.func @union_whole_use
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_union
    // CHECK: simulation.ref.load {{.*}} -> !simulation.unpacked_union
    simulation.func @union_whole_use(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32}) -> !choice
        attributes {entry_kind = 8 : i32, code_unit_id = 9000020 : i64} {
      %initial = simulation.union.construct %value as 0 : (i8) -> !choice
      %local = simulation.ref.alloc %initial : !choice -> !simulation.ref<!choice>
      %loaded = simulation.ref.load %local : !simulation.ref<!choice> -> !choice
      simulation.return %loaded : !choice
    }

    // Tagged-union defaults have no selected field and retain shared backing.
    // CHECK-LABEL: simulation.func @tagged_default
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_union
    // CHECK: simulation.ref.subelement
    simulation.func @tagged_default(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000021 : i64} {
      %default = simulation.aggregate.default : !tagged_choice
      %local = simulation.ref.alloc %default : !tagged_choice -> !simulation.ref<!tagged_choice>
      %field = simulation.ref.subelement %local[[0]] : !simulation.ref<!tagged_choice> -> !simulation.ref<i8>
      %loaded = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // A four-state packed tagged default has an unknown tag and retains backing.
    // CHECK-LABEL: simulation.func @packed_tagged_default
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.packed_union
    // CHECK: simulation.ref.subelement
    simulation.func @packed_tagged_default(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000022 : i64} {
      %default = simulation.aggregate.default : !packed_tagged_logic_choice
      %local = simulation.ref.alloc %default : !packed_tagged_logic_choice -> !simulation.ref<!packed_tagged_logic_choice>
      %field = simulation.ref.subelement %local[[0]] : !simulation.ref<!packed_tagged_logic_choice> -> !simulation.ref<!simulation.logic<8>>
      %loaded = simulation.ref.load %field : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %loaded : !simulation.logic<8>
    }

    // A two-state packed tagged default has tag zero, selecting field zero.
    // CHECK-LABEL: simulation.func @packed_tagged_two_state_default
    // CHECK-NOT: !simulation.ref<!simulation.packed_union
    // CHECK: simulation.ref.alloc {{.*}} : i8 -> !simulation.ref<i8>
    simulation.func @packed_tagged_two_state_default(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000023 : i64} {
      %default = simulation.aggregate.default : !packed_tagged_choice
      %local = simulation.ref.alloc %default : !packed_tagged_choice -> !simulation.ref<!packed_tagged_choice>
      %field = simulation.ref.subelement %local[[0]] : !simulation.ref<!packed_tagged_choice> -> !simulation.ref<i8>
      %loaded = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }

    // An escaping reference is a blocking use and retains shared backing.
    // CHECK-LABEL: simulation.func @escaping_reference
    // CHECK: simulation.ref.alloc {{.*}} : !simulation.unpacked_struct
    // CHECK: simulation.call @consume
    simulation.func @escaping_reference(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000024 : i64} {
      %default = simulation.aggregate.default : !pair
      %local = simulation.ref.alloc %default : !pair -> !simulation.ref<!pair>
      %result = simulation.call @consume(%ctx, %local) : (!simulation.context, !simulation.ref<!pair>) -> i8
      simulation.return %result : i8
    }

    simulation.func private @consume(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<!pair> {simulation.capture_kind = 1 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000025 : i64} {
      %field = simulation.ref.subelement %value[[0]] : !simulation.ref<!pair> -> !simulation.ref<i8>
      %loaded = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %loaded : i8
    }
  }
}
