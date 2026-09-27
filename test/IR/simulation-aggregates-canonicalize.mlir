// RUN: obelisk-opt %s --canonicalize | FileCheck %s

!record = !simulation.unpacked_struct<[
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "flag", type = i1, ordinal = 1, packedOffset = 0>
]>
!outer = !simulation.unpacked_struct<[
  #simulation.field<name = "record", type = !record, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "tail", type = i16, ordinal = 1, packedOffset = 0>
]>
!array = !simulation.unpacked_array<3 : 1 x i8>
!record_array = !simulation.unpacked_array<1 : 0 x !record>
!packed = !simulation.packed_array<1 : 0 x i8>
!choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = false>
!tagged_choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = true>

// CHECK-LABEL: func.func @extract_construct
// CHECK-NEXT: return %arg1 : i8
func.func @extract_construct(%a: i8, %b: i8) -> i8 {
  %array = simulation.aggregate.construct %a, %b, %a : (i8, i8, i8) -> !array
  %value = simulation.aggregate.extract %array[1] : (!array) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @extract_default
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
// CHECK-NEXT: return %[[ZERO]] : i8
func.func @extract_default() -> i8 {
  %default = simulation.aggregate.default : !record
  %value = simulation.aggregate.extract %default[0] : (!record) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @insert_extract
// CHECK-NEXT: return %arg2 : i8
func.func @insert_extract(%a: i8, %flag: i1, %replacement: i8) -> i8 {
  %record = simulation.aggregate.construct %a, %flag : (i8, i1) -> !record
  %updated = simulation.aggregate.insert %replacement into %record[0] : (!record, i8) -> !record
  %value = simulation.aggregate.extract %updated[0] : (!record) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @extract_other_insert
// CHECK: %[[VALUE:.*]] = simulation.aggregate.extract %arg0[1]
// CHECK-NEXT: return %[[VALUE]] : i1
func.func @extract_other_insert(%record: !record, %replacement: i8) -> i1 {
  %updated = simulation.aggregate.insert %replacement into %record[0] : (!record, i8) -> !record
  %value = simulation.aggregate.extract %updated[1] : (!record) -> i1
  return %value : i1
}

// CHECK-LABEL: func.func @overwritten_insert
// CHECK: simulation.aggregate.insert %arg2 into %arg0[0]
func.func @overwritten_insert(%record: !record, %first: i8, %second: i8) -> !record {
  %once = simulation.aggregate.insert %first into %record[0] : (!record, i8) -> !record
  %twice = simulation.aggregate.insert %second into %once[0] : (!record, i8) -> !record
  return %twice : !record
}

// CHECK-LABEL: func.func @reconstruct
// CHECK-NEXT: return %arg0 : !simulation.unpacked_struct
func.func @reconstruct(%record: !record) -> !record {
  %byte = simulation.aggregate.extract %record[0] : (!record) -> i8
  %flag = simulation.aggregate.extract %record[1] : (!record) -> i1
  %copy = simulation.aggregate.construct %byte, %flag : (i8, i1) -> !record
  return %copy : !record
}

// CHECK-LABEL: func.func @redundant_insert
// CHECK-NEXT: return %arg0 : !simulation.unpacked_struct
func.func @redundant_insert(%record: !record) -> !record {
  %byte = simulation.aggregate.extract %record[0] : (!record) -> i8
  %copy = simulation.aggregate.insert %byte into %record[0] : (!record, i8) -> !record
  return %copy : !record
}

// CHECK-LABEL: func.func @constant_dynamic
// CHECK-NEXT: return %arg1 : i8
func.func @constant_dynamic(%a: i8, %b: i8, %c: i8) -> i8 {
  %index = arith.constant 2 : i32
  %array = simulation.aggregate.construct %a, %b, %c : (i8, i8, i8) -> !array
  %value = simulation.array.extract_dynamic %array[%index] : (!array, i32) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @invalid_dynamic
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
// CHECK-NEXT: return %[[ZERO]] : i8
func.func @invalid_dynamic(%array: !array) -> i8 {
  %index = arith.constant 0 : i32
  %value = simulation.array.extract_dynamic %array[%index] : (!array, i32) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @unknown_dynamic
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
// CHECK-NEXT: return %[[ZERO]] : i8
func.func @unknown_dynamic(%array: !array) -> i8 {
  %index = simulation.logic.constant 0 : i32, -1 : i32 : !simulation.logic<32>
  %value = simulation.array.extract_dynamic %array[%index] : (!array, !simulation.logic<32>) -> i8
  return %value : i8
}

// IEEE 1800-2017 7.4.6 gives a constant index the same element a static ordinal
// names, so a dynamic write through one folds to the static insertion.
// CHECK-LABEL: func.func @constant_dynamic_insert
// CHECK: %[[UPDATED:.*]] = simulation.aggregate.insert %arg1 into %arg0[1]
// CHECK-NEXT: return %[[UPDATED]] : !simulation.unpacked_array<3 : 1 x i8>
func.func @constant_dynamic_insert(%array: !array, %value: i8) -> !array {
  %index = arith.constant 2 : i32
  %updated = simulation.array.insert_dynamic %value into %array[%index] : (!array, i8, i32) -> !array
  return %updated : !array
}

// IEEE 1800-2017 7.4.6: "Writing to an array with an invalid index shall
// perform no operation", so an out-of-range index leaves the array as it was.
// CHECK-LABEL: func.func @invalid_dynamic_insert
// CHECK-NEXT: return %arg0 : !simulation.unpacked_array<3 : 1 x i8>
func.func @invalid_dynamic_insert(%array: !array, %value: i8) -> !array {
  %index = arith.constant 0 : i32
  %updated = simulation.array.insert_dynamic %value into %array[%index] : (!array, i8, i32) -> !array
  return %updated : !array
}

// The same clause makes an index with an unknown bit invalid.
// CHECK-LABEL: func.func @unknown_dynamic_insert
// CHECK-NEXT: return %arg0 : !simulation.unpacked_array<3 : 1 x i8>
func.func @unknown_dynamic_insert(%array: !array, %value: i8) -> !array {
  %index = simulation.logic.constant 0 : i32, -1 : i32 : !simulation.logic<32>
  %updated = simulation.array.insert_dynamic %value into %array[%index] : (!array, i8, !simulation.logic<32>) -> !array
  return %updated : !array
}

// Out-of-range reads of aggregate elements recursively materialize defaults.
// CHECK-LABEL: func.func @aggregate_element_oob
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i8
// CHECK-NEXT: return %[[ZERO]] : i8
func.func @aggregate_element_oob(%array: !record_array) -> i8 {
  %index = arith.constant 5 : i32
  %record = simulation.array.extract_dynamic %array[%index] : (!record_array, i32) -> !record
  %byte = simulation.aggregate.extract %record[0] : (!record) -> i8
  return %byte : i8
}

// CHECK-LABEL: func.func @four_state_default
// CHECK: %[[UNKNOWN:.*]] = simulation.logic.constant 0 : i8, -1 : i8
// CHECK-NEXT: return %[[UNKNOWN]] : !simulation.logic<8>
func.func @four_state_default() -> !simulation.logic<8> {
  %default = simulation.aggregate.default : !simulation.unpacked_array<1 : 0 x !simulation.logic<8>>
  %value = simulation.aggregate.extract %default[0] : (!simulation.unpacked_array<1 : 0 x !simulation.logic<8>>) -> !simulation.logic<8>
  return %value : !simulation.logic<8>
}

// CHECK-LABEL: func.func @nested_default
// CHECK: %[[ZERO:.*]] = arith.constant false
// CHECK-NEXT: return %[[ZERO]] : i1
func.func @nested_default() -> i1 {
  %default = simulation.aggregate.default : !outer
  %record = simulation.aggregate.extract %default[0] : (!outer) -> !record
  %flag = simulation.aggregate.extract %record[1] : (!record) -> i1
  return %flag : i1
}

// CHECK-LABEL: func.func @flatten_path
// CHECK: simulation.ref.subelement %arg0{{\[\[0, 1\]\]}}
func.func @flatten_path(%outer: !simulation.ref<!outer>) -> !simulation.ref<i1> {
  %record = simulation.ref.subelement %outer[[0]] : !simulation.ref<!outer> -> !simulation.ref<!record>
  %flag = simulation.ref.subelement %record[[1]] : !simulation.ref<!record> -> !simulation.ref<i1>
  return %flag : !simulation.ref<i1>
}

// CHECK-LABEL: func.func @flatten_driver_path
// CHECK: simulation.driver.subelement %arg0{{\[\[0, 1\]\]}}
func.func @flatten_driver_path(%outer: !simulation.driver<!outer>) -> !simulation.driver<i1> {
  %record = simulation.driver.subelement %outer[[0]] : !simulation.driver<!outer> -> !simulation.driver<!record>
  %flag = simulation.driver.subelement %record[[1]] : !simulation.driver<!record> -> !simulation.driver<i1>
  return %flag : !simulation.driver<i1>
}

// CHECK-LABEL: func.func @direct_field_load
// CHECK: %[[VIEW:.*]] = simulation.ref.subelement %arg0{{\[\[0\]\]}}
// CHECK: %[[VALUE:.*]] = simulation.ref.load %[[VIEW]]
// CHECK-NEXT: return %[[VALUE]] : i8
func.func @direct_field_load(%record: !simulation.ref<!record>) -> i8 {
  %loaded = simulation.ref.load %record : !simulation.ref<!record> -> !record
  %byte = simulation.aggregate.extract %loaded[0] : (!record) -> i8
  return %byte : i8
}

// The narrowed load must remain at the whole load's snapshot point.
// CHECK-LABEL: func.func @snapshot_field_load
// CHECK: %[[EARLY_VIEW:.*]] = simulation.ref.subelement %arg0{{\[\[0\]\]}}
// CHECK-NEXT: %[[OLD:.*]] = simulation.ref.load %[[EARLY_VIEW]]
// CHECK: simulation.ref.store %arg1
// CHECK-NEXT: return %[[OLD]] : i8
func.func @snapshot_field_load(%record: !simulation.ref<!record>, %new: i8) -> i8 {
  %snapshot = simulation.ref.load %record : !simulation.ref<!record> -> !record
  %field = simulation.ref.subelement %record[[0]] : !simulation.ref<!record> -> !simulation.ref<i8>
  simulation.ref.store %new to %field : i8, !simulation.ref<i8>
  %old = simulation.aggregate.extract %snapshot[0] : (!record) -> i8
  return %old : i8
}

// Union field-load formation observes the same snapshot ordering rule.
// CHECK-LABEL: func.func @snapshot_union_load
// CHECK: %[[UNION_VIEW:.*]] = simulation.ref.subelement %arg0{{\[\[0\]\]}}
// CHECK-NEXT: %[[UNION_OLD:.*]] = simulation.ref.load %[[UNION_VIEW]]
// CHECK: simulation.ref.store %arg1
// CHECK-NEXT: return %[[UNION_OLD]] : i8
func.func @snapshot_union_load(%choice: !simulation.ref<!choice>, %new: i8) -> i8 {
  %snapshot = simulation.ref.load %choice : !simulation.ref<!choice> -> !choice
  %field = simulation.ref.subelement %choice[[0]] : !simulation.ref<!choice> -> !simulation.ref<i8>
  simulation.ref.store %new to %field : i8, !simulation.ref<i8>
  %old = simulation.union.extract %snapshot[0] : (!choice) -> i8
  return %old : i8
}

// CHECK-LABEL: func.func @constant_ref_array_view
// CHECK: simulation.ref.subelement %arg0{{\[\[2\]\]}}
func.func @constant_ref_array_view(%array: !simulation.ref<!array>) -> !simulation.ref<i8> {
  %index = arith.constant 1 : i32
  %element = simulation.ref.array_element %array[%index] : (!simulation.ref<!array>, i32) -> !simulation.ref<i8>
  return %element : !simulation.ref<i8>
}

// CHECK-LABEL: func.func @constant_driver_array_view
// CHECK: simulation.driver.subelement %arg0{{\[\[2\]\]}}
func.func @constant_driver_array_view(%array: !simulation.driver<!array>) -> !simulation.driver<i8> {
  %index = arith.constant 1 : i32
  %element = simulation.driver.array_element %array[%index] : (!simulation.driver<!array>, i32) -> !simulation.driver<i8>
  return %element : !simulation.driver<i8>
}

// CHECK-LABEL: func.func @matching_union_construct
// CHECK-NEXT: return %arg0 : i16
func.func @matching_union_construct(%word: i16) -> i16 {
  %choice = simulation.union.construct %word as 1 : (i16) -> !choice
  %value = simulation.union.extract %choice[1] : (!choice) -> i16
  return %value : i16
}

// A tagged default has no selected field and must not fold.
// CHECK-LABEL: func.func @tagged_union_default
// CHECK: %[[DEFAULT:.*]] = simulation.aggregate.default : !simulation.unpacked_union
// CHECK-NEXT: %[[VALUE:.*]] = simulation.union.extract %[[DEFAULT]][0]
// CHECK-NEXT: return %[[VALUE]] : i8
func.func @tagged_union_default() -> i8 {
  %default = simulation.aggregate.default : !tagged_choice
  %value = simulation.union.extract %default[0] : (!tagged_choice) -> i8
  return %value : i8
}

// CHECK-LABEL: func.func @packed_inverse
// CHECK-NEXT: return %arg0 : !simulation.packed_array
func.func @packed_inverse(%packed: !packed) -> !packed {
  %bits = simulation.packed.flatten %packed : (!packed) -> i16
  %copy = simulation.packed.unflatten %bits : (i16) -> !packed
  return %copy : !packed
}

// CHECK-LABEL: func.func @scalar_inverse
// CHECK-NEXT: return %arg0 : i16
func.func @scalar_inverse(%bits: i16) -> i16 {
  %packed = simulation.packed.unflatten %bits : (i16) -> !packed
  %copy = simulation.packed.flatten %packed : (!packed) -> i16
  return %copy : i16
}

// A dynamic read of a loaded array addresses the element instead, so a large
// array never becomes a multi-kilobit value indexed by a full-width shift.
// CHECK-LABEL: func.func @dynamic_extract_through_reference
// CHECK-NOT: simulation.ref.load %arg0
// CHECK: %[[ELEMENT:.*]] = simulation.ref.array_element %arg0[%arg1]
// CHECK: %[[VALUE:.*]] = simulation.ref.load %[[ELEMENT]]
// CHECK: return %[[VALUE]] : i8
func.func @dynamic_extract_through_reference(
    %reference: !simulation.ref<!array>, %index: i32) -> i8 {
  %array = simulation.ref.load %reference : !simulation.ref<!array> -> !array
  %value = simulation.array.extract_dynamic %array[%index] : (!array, i32) -> i8
  return %value : i8
}

// The narrow read may not sink past a store: it would observe a newer value
// than the wide load did.
// CHECK-LABEL: func.func @dynamic_extract_keeps_store_order
// CHECK: simulation.ref.load %arg0
// CHECK: simulation.ref.store
// CHECK: simulation.array.extract_dynamic
func.func @dynamic_extract_keeps_store_order(
    %reference: !simulation.ref<!array>, %index: i32, %replacement: !array) -> i8 {
  %array = simulation.ref.load %reference : !simulation.ref<!array> -> !array
  simulation.ref.store %replacement to %reference : !array, !simulation.ref<!array>
  %value = simulation.array.extract_dynamic %array[%index] : (!array, i32) -> i8
  return %value : i8
}
