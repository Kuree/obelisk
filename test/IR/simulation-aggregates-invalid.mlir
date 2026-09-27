// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

// expected-error @+1 {{aggregate field ordinals must be dense and ordered}}
func.func private @bad_ordinal(%arg: !simulation.unpacked_struct<[#simulation.field<name = "a", type = i8, ordinal = 1, packedOffset = 0>]>)

// -----

// expected-error @+1 {{aggregate field names must be unique}}
func.func private @duplicate_name(%arg: !simulation.unpacked_struct<[#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "a", type = i16, ordinal = 1, packedOffset = 0>]>)

// -----

// expected-error @+1 {{unpacked aggregate field has a packed offset}}
func.func private @unpacked_offset(%arg: !simulation.unpacked_struct<[#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 1>]>)

// -----

// expected-error @+1 {{packed struct fields overlap}}
func.func private @packed_overlap(%arg: !simulation.packed_struct<[#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i8, ordinal = 1, packedOffset = 4>]>)

// -----

// expected-error @+1 {{packed struct fields must be contiguous}}
func.func private @packed_gap(%arg: !simulation.packed_struct<[#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i8, ordinal = 1, packedOffset = 16>]>)

// -----

// expected-error @+1 {{packed struct fields must cover bit zero}}
func.func private @packed_no_bit_zero(%arg: !simulation.packed_struct<[#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 1>]>)

// -----

// expected-error @+1 {{packed union fields must start at bit zero}}
func.func private @packed_union_offset(%arg: !simulation.packed_union<fields = [#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i8, ordinal = 1, packedOffset = 8>], isTagged = false>)

// -----

// expected-error @+1 {{untagged packed union fields must have equal widths}}
func.func private @packed_union_width(%arg: !simulation.packed_union<fields = [#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i16, ordinal = 1, packedOffset = 0>], isTagged = false>)

// -----

// expected-error @+1 {{packed tagged union requires 1 tag bits}}
func.func private @packed_union_tag_width(%arg: !simulation.packed_union<fields = [#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i16, ordinal = 1, packedOffset = 0>], isTagged = true, tagBits = 2>)

// -----

// expected-error @+1 {{packed array element must be packed}}
func.func private @packed_unpacked_element(%arg: !simulation.packed_array<1 : 0 x !simulation.unpacked_array<1 : 0 x i8>>)

// -----

func.func @bad_construct(%arg: i8) {
  // expected-error @+1 {{requires one operand per aggregate element}}
  %value = simulation.aggregate.construct %arg : (i8) -> !simulation.unpacked_array<1 : 0 x i8>
  return
}

// -----

func.func @bad_construct_type(%arg: i8, %other: i16) {
  // expected-error @+1 {{operand #1 does not match its aggregate element type}}
  %value = simulation.aggregate.construct %arg, %other : (i8, i16) -> !simulation.unpacked_array<1 : 0 x i8>
  return
}

// -----

func.func @bad_default() {
  // expected-error @+1 {{result must be a fixed aggregate type}}
  %value = simulation.aggregate.default : i8
  return
}

// -----

func.func @bad_extract(%arg: !simulation.unpacked_array<1 : 0 x i8>) {
  // expected-error @+1 {{aggregate index is out of range}}
  %value = simulation.aggregate.extract %arg[2] : (!simulation.unpacked_array<1 : 0 x i8>) -> i8
  return
}

// -----

func.func @bad_dynamic(%arg: !simulation.unpacked_struct<[#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>]>, %index: i32) {
  // expected-error @+1 {{input must be a fixed array}}
  %value = simulation.array.extract_dynamic %arg[%index] : (!simulation.unpacked_struct<[#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>]>, i32) -> i8
  return
}

// -----

func.func @bad_dynamic_result(%arg: !simulation.unpacked_array<1 : 0 x i8>, %index: i32) {
  // expected-error @+1 {{result must match the array element type}}
  %value = simulation.array.extract_dynamic %arg[%index] : (!simulation.unpacked_array<1 : 0 x i8>, i32) -> i16
  return
}

// -----

func.func @bad_dynamic_index(%arg: !simulation.unpacked_array<1 : 0 x i8>, %index: f32) {
  // expected-error @+1 {{index must be a signless builtin integer or four-state logic}}
  %value = simulation.array.extract_dynamic %arg[%index] : (!simulation.unpacked_array<1 : 0 x i8>, f32) -> i8
  return
}

// -----

func.func @bad_insert(%arg: !simulation.unpacked_array<1 : 0 x i8>, %replacement: i16) {
  // expected-error @+1 {{result type must match aggregate element type}}
  %value = simulation.aggregate.insert %replacement into %arg[0] : (!simulation.unpacked_array<1 : 0 x i8>, i16) -> !simulation.unpacked_array<1 : 0 x i8>
  return
}

// -----

func.func @bad_union_construct(%value: i16) {
  // expected-error @+1 {{result type must match aggregate element type}}
  %choice = simulation.union.construct %value as 0 : (i16) -> !simulation.unpacked_union<fields = [#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>], isTagged = false>
  return
}

// -----

func.func @bad_path(%arg: !simulation.ref<!simulation.unpacked_array<1 : 0 x i8>>) {
  // expected-error @+1 {{subelement path is out of range}}
  %value = simulation.ref.subelement %arg[[2]] : !simulation.ref<!simulation.unpacked_array<1 : 0 x i8>> -> !simulation.ref<i8>
  return
}

// -----

func.func @bad_path_type(%arg: !simulation.ref<!simulation.unpacked_array<1 : 0 x i8>>) {
  // expected-error @+1 {{result element type must match selected subelement}}
  %value = simulation.ref.subelement %arg[[0]] : !simulation.ref<!simulation.unpacked_array<1 : 0 x i8>> -> !simulation.ref<i16>
  return
}

// -----

func.func @bad_alloc(%arg: i8) {
  // expected-error @+1 {{initial value must match allocated element type}}
  %value = simulation.ref.alloc %arg : i8 -> !simulation.ref<i16>
  return
}

// -----

func.func @bad_flatten(%arg: !simulation.packed_array<1 : 0 x i8>) {
  // expected-error @+1 {{result must be the aggregate's width- and state-matched scalar}}
  %value = simulation.packed.flatten %arg : (!simulation.packed_array<1 : 0 x i8>) -> i8
  return
}

// -----

func.func @flatten_unpacked(%arg: !simulation.unpacked_array<1 : 0 x i8>) {
  // expected-error @+1 {{input must be a packed aggregate}}
  %value = simulation.packed.flatten %arg : (!simulation.unpacked_array<1 : 0 x i8>) -> i16
  return
}

// -----

// expected-error @+1 {{aggregate requires at least one field}}
func.func private @empty_aggregate(%arg: !simulation.unpacked_struct<[]>)

// -----

func.func private @bad_field_type(%arg: !simulation.unpacked_struct<[#simulation.field<name = "a", type = f32, ordinal = 0, packedOffset = 0>]>)

// -----

// expected-error @+1 {{fixed array range is too large}}
func.func private @oversized_array(%arg: !simulation.unpacked_array<9223372036854775807 : -9223372036854775808 x i8>)
