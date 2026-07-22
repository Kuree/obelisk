// RUN: obelisk-opt %s --canonicalize | FileCheck %s

!record = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "flag", type = i1, ordinal = 1, packedOffset = 0>
]>
!outer = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "record", type = !record, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "tail", type = i16, ordinal = 1, packedOffset = 0>
]>
!array = !obelisk_sim.unpacked_array<3 : 1 x i8>
!record_array = !obelisk_sim.unpacked_array<1 : 0 x !record>
!packed = !obelisk_sim.packed_array<1 : 0 x i8>
!choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = false>
!tagged_choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = true>

// CHECK-LABEL: func.func @extract_construct
// CHECK-NEXT: return %arg1 : i8
func.func @extract_construct(%a: i8, %b: i8) -> i8 {
  %array = obelisk_sim.aggregate.construct %a, %b, %a : (i8, i8, i8) -> !array
  %value = obelisk_sim.aggregate.extract %array[1] : (!array) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @extract_default
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
// CHECK-NEXT: return %[[ZERO]] : i8
func.func @extract_default() -> i8 {
  %default = obelisk_sim.aggregate.default : !record
  %value = obelisk_sim.aggregate.extract %default[0] : (!record) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @insert_extract
// CHECK-NEXT: return %arg2 : i8
func.func @insert_extract(%a: i8, %flag: i1, %replacement: i8) -> i8 {
  %record = obelisk_sim.aggregate.construct %a, %flag : (i8, i1) -> !record
  %updated = obelisk_sim.aggregate.insert %replacement into %record[0] : (!record, i8) -> !record
  %value = obelisk_sim.aggregate.extract %updated[0] : (!record) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @extract_other_insert
// CHECK: %[[VALUE:.*]] = obelisk_sim.aggregate.extract %arg0[1]
// CHECK-NEXT: return %[[VALUE]] : i1
func.func @extract_other_insert(%record: !record, %replacement: i8) -> i1 {
  %updated = obelisk_sim.aggregate.insert %replacement into %record[0] : (!record, i8) -> !record
  %value = obelisk_sim.aggregate.extract %updated[1] : (!record) -> i1
  return %value : i1
}

// CHECK-LABEL: func.func @overwritten_insert
// CHECK: obelisk_sim.aggregate.insert %arg2 into %arg0[0]
func.func @overwritten_insert(%record: !record, %first: i8, %second: i8) -> !record {
  %once = obelisk_sim.aggregate.insert %first into %record[0] : (!record, i8) -> !record
  %twice = obelisk_sim.aggregate.insert %second into %once[0] : (!record, i8) -> !record
  return %twice : !record
}

// CHECK-LABEL: func.func @reconstruct
// CHECK-NEXT: return %arg0 : !obelisk_sim.unpacked_struct
func.func @reconstruct(%record: !record) -> !record {
  %byte = obelisk_sim.aggregate.extract %record[0] : (!record) -> i8
  %flag = obelisk_sim.aggregate.extract %record[1] : (!record) -> i1
  %copy = obelisk_sim.aggregate.construct %byte, %flag : (i8, i1) -> !record
  return %copy : !record
}

// CHECK-LABEL: func.func @redundant_insert
// CHECK-NEXT: return %arg0 : !obelisk_sim.unpacked_struct
func.func @redundant_insert(%record: !record) -> !record {
  %byte = obelisk_sim.aggregate.extract %record[0] : (!record) -> i8
  %copy = obelisk_sim.aggregate.insert %byte into %record[0] : (!record, i8) -> !record
  return %copy : !record
}

// CHECK-LABEL: func.func @constant_dynamic
// CHECK-NEXT: return %arg1 : i8
func.func @constant_dynamic(%a: i8, %b: i8, %c: i8) -> i8 {
  %index = arith.constant 2 : i32
  %array = obelisk_sim.aggregate.construct %a, %b, %c : (i8, i8, i8) -> !array
  %value = obelisk_sim.array.extract_dynamic %array[%index] : (!array, i32) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @invalid_dynamic
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
// CHECK-NEXT: return %[[ZERO]] : i8
func.func @invalid_dynamic(%array: !array) -> i8 {
  %index = arith.constant 0 : i32
  %value = obelisk_sim.array.extract_dynamic %array[%index] : (!array, i32) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @unknown_dynamic
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
// CHECK-NEXT: return %[[ZERO]] : i8
func.func @unknown_dynamic(%array: !array) -> i8 {
  %index = obelisk_sim.logic.constant 0 : i32, -1 : i32 : !obelisk_sim.logic<32>
  %value = obelisk_sim.array.extract_dynamic %array[%index] : (!array, !obelisk_sim.logic<32>) -> i8
  return %value : i8
}

// Out-of-range reads of aggregate elements recursively materialize defaults.
// CHECK-LABEL: func.func @aggregate_element_oob
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
// CHECK-NEXT: return %[[ZERO]] : i8
func.func @aggregate_element_oob(%array: !record_array) -> i8 {
  %index = arith.constant 5 : i32
  %record = obelisk_sim.array.extract_dynamic %array[%index] : (!record_array, i32) -> !record
  %byte = obelisk_sim.aggregate.extract %record[0] : (!record) -> i8
  return %byte : i8
}

// CHECK-LABEL: func.func @four_state_default
// CHECK: %[[UNKNOWN:.*]] = obelisk_sim.logic.constant 0 : i8, -1 : i8
// CHECK-NEXT: return %[[UNKNOWN]] : !obelisk_sim.logic<8>
func.func @four_state_default() -> !obelisk_sim.logic<8> {
  %default = obelisk_sim.aggregate.default : !obelisk_sim.unpacked_array<1 : 0 x !obelisk_sim.logic<8>>
  %value = obelisk_sim.aggregate.extract %default[0] : (!obelisk_sim.unpacked_array<1 : 0 x !obelisk_sim.logic<8>>) -> !obelisk_sim.logic<8>
  return %value : !obelisk_sim.logic<8>
}

// CHECK-LABEL: func.func @nested_default
// CHECK: %[[ZERO:.*]] = arith.constant false
// CHECK-NEXT: return %[[ZERO]] : i1
func.func @nested_default() -> i1 {
  %default = obelisk_sim.aggregate.default : !outer
  %record = obelisk_sim.aggregate.extract %default[0] : (!outer) -> !record
  %flag = obelisk_sim.aggregate.extract %record[1] : (!record) -> i1
  return %flag : i1
}

// CHECK-LABEL: func.func @flatten_path
// CHECK: obelisk_sim.ref.subelement %arg0{{\[\[0, 1\]\]}}
func.func @flatten_path(%outer: !obelisk_sim.ref<!outer>) -> !obelisk_sim.ref<i1> {
  %record = obelisk_sim.ref.subelement %outer[[0]] : !obelisk_sim.ref<!outer> -> !obelisk_sim.ref<!record>
  %flag = obelisk_sim.ref.subelement %record[[1]] : !obelisk_sim.ref<!record> -> !obelisk_sim.ref<i1>
  return %flag : !obelisk_sim.ref<i1>
}

// CHECK-LABEL: func.func @flatten_driver_path
// CHECK: obelisk_sim.driver.subelement %arg0{{\[\[0, 1\]\]}}
func.func @flatten_driver_path(%outer: !obelisk_sim.driver<!outer>) -> !obelisk_sim.driver<i1> {
  %record = obelisk_sim.driver.subelement %outer[[0]] : !obelisk_sim.driver<!outer> -> !obelisk_sim.driver<!record>
  %flag = obelisk_sim.driver.subelement %record[[1]] : !obelisk_sim.driver<!record> -> !obelisk_sim.driver<i1>
  return %flag : !obelisk_sim.driver<i1>
}

// CHECK-LABEL: func.func @direct_field_load
// CHECK: %[[VIEW:.*]] = obelisk_sim.ref.subelement %arg0{{\[\[0\]\]}}
// CHECK: %[[VALUE:.*]] = obelisk_sim.ref.load %[[VIEW]]
// CHECK-NEXT: return %[[VALUE]] : i8
func.func @direct_field_load(%record: !obelisk_sim.ref<!record>) -> i8 {
  %loaded = obelisk_sim.ref.load %record : !obelisk_sim.ref<!record> -> !record
  %byte = obelisk_sim.aggregate.extract %loaded[0] : (!record) -> i8
  return %byte : i8
}

// The narrowed load must remain at the whole load's snapshot point.
// CHECK-LABEL: func.func @snapshot_field_load
// CHECK: %[[EARLY_VIEW:.*]] = obelisk_sim.ref.subelement %arg0{{\[\[0\]\]}}
// CHECK-NEXT: %[[OLD:.*]] = obelisk_sim.ref.load %[[EARLY_VIEW]]
// CHECK: obelisk_sim.ref.store %arg1
// CHECK-NEXT: return %[[OLD]] : i8
func.func @snapshot_field_load(%record: !obelisk_sim.ref<!record>, %new: i8) -> i8 {
  %snapshot = obelisk_sim.ref.load %record : !obelisk_sim.ref<!record> -> !record
  %field = obelisk_sim.ref.subelement %record[[0]] : !obelisk_sim.ref<!record> -> !obelisk_sim.ref<i8>
  obelisk_sim.ref.store %new to %field : i8, !obelisk_sim.ref<i8>
  %old = obelisk_sim.aggregate.extract %snapshot[0] : (!record) -> i8
  return %old : i8
}

// Union field-load formation observes the same snapshot ordering rule.
// CHECK-LABEL: func.func @snapshot_union_load
// CHECK: %[[UNION_VIEW:.*]] = obelisk_sim.ref.subelement %arg0{{\[\[0\]\]}}
// CHECK-NEXT: %[[UNION_OLD:.*]] = obelisk_sim.ref.load %[[UNION_VIEW]]
// CHECK: obelisk_sim.ref.store %arg1
// CHECK-NEXT: return %[[UNION_OLD]] : i8
func.func @snapshot_union_load(%choice: !obelisk_sim.ref<!choice>, %new: i8) -> i8 {
  %snapshot = obelisk_sim.ref.load %choice : !obelisk_sim.ref<!choice> -> !choice
  %field = obelisk_sim.ref.subelement %choice[[0]] : !obelisk_sim.ref<!choice> -> !obelisk_sim.ref<i8>
  obelisk_sim.ref.store %new to %field : i8, !obelisk_sim.ref<i8>
  %old = obelisk_sim.union.extract %snapshot[0] : (!choice) -> i8
  return %old : i8
}

// CHECK-LABEL: func.func @constant_ref_array_view
// CHECK: obelisk_sim.ref.subelement %arg0{{\[\[2\]\]}}
func.func @constant_ref_array_view(%array: !obelisk_sim.ref<!array>) -> !obelisk_sim.ref<i8> {
  %index = arith.constant 1 : i32
  %element = obelisk_sim.ref.array_element %array[%index] : (!obelisk_sim.ref<!array>, i32) -> !obelisk_sim.ref<i8>
  return %element : !obelisk_sim.ref<i8>
}

// CHECK-LABEL: func.func @constant_driver_array_view
// CHECK: obelisk_sim.driver.subelement %arg0{{\[\[2\]\]}}
func.func @constant_driver_array_view(%array: !obelisk_sim.driver<!array>) -> !obelisk_sim.driver<i8> {
  %index = arith.constant 1 : i32
  %element = obelisk_sim.driver.array_element %array[%index] : (!obelisk_sim.driver<!array>, i32) -> !obelisk_sim.driver<i8>
  return %element : !obelisk_sim.driver<i8>
}

// CHECK-LABEL: func.func @matching_union_construct
// CHECK-NEXT: return %arg0 : i16
func.func @matching_union_construct(%word: i16) -> i16 {
  %choice = obelisk_sim.union.construct %word as 1 : (i16) -> !choice
  %value = obelisk_sim.union.extract %choice[1] : (!choice) -> i16
  return %value : i16
}

// A tagged default has no selected field and must not fold.
// CHECK-LABEL: func.func @tagged_union_default
// CHECK: %[[DEFAULT:.*]] = obelisk_sim.aggregate.default : !obelisk_sim.unpacked_union
// CHECK-NEXT: %[[VALUE:.*]] = obelisk_sim.union.extract %[[DEFAULT]][0]
// CHECK-NEXT: return %[[VALUE]] : i8
func.func @tagged_union_default() -> i8 {
  %default = obelisk_sim.aggregate.default : !tagged_choice
  %value = obelisk_sim.union.extract %default[0] : (!tagged_choice) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @packed_inverse
// CHECK-NEXT: return %arg0 : !obelisk_sim.packed_array
func.func @packed_inverse(%packed: !packed) -> !packed {
  %bits = obelisk_sim.packed.flatten %packed : (!packed) -> i16
  %copy = obelisk_sim.packed.unflatten %bits : (i16) -> !packed
  return %copy : !packed
}

// CHECK-LABEL: func.func @scalar_inverse
// CHECK-NEXT: return %arg0 : i16
func.func @scalar_inverse(%bits: i16) -> i16 {
  %packed = obelisk_sim.packed.unflatten %bits : (i16) -> !packed
  %copy = obelisk_sim.packed.flatten %packed : (!packed) -> i16
  return %copy : i16
}
