// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

// expected-error @+1 {{aggregate field ordinals must be dense and ordered}}
func.func private @bad_ordinal(%arg: !obelisk_sim.unpacked_struct<[#obelisk_sim.field<name = "a", type = i8, ordinal = 1, packedOffset = 0>]>)

// -----

// expected-error @+1 {{aggregate field names must be unique}}
func.func private @duplicate_name(%arg: !obelisk_sim.unpacked_struct<[#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "a", type = i16, ordinal = 1, packedOffset = 0>]>)

// -----

// expected-error @+1 {{unpacked aggregate field has a packed offset}}
func.func private @unpacked_offset(%arg: !obelisk_sim.unpacked_struct<[#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 1>]>)

// -----

// expected-error @+1 {{packed struct fields overlap}}
func.func private @packed_overlap(%arg: !obelisk_sim.packed_struct<[#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "b", type = i8, ordinal = 1, packedOffset = 4>]>)

// -----

// expected-error @+1 {{packed struct fields must be contiguous}}
func.func private @packed_gap(%arg: !obelisk_sim.packed_struct<[#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "b", type = i8, ordinal = 1, packedOffset = 16>]>)

// -----

// expected-error @+1 {{packed struct fields must cover bit zero}}
func.func private @packed_no_bit_zero(%arg: !obelisk_sim.packed_struct<[#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 1>]>)

// -----

// expected-error @+1 {{packed union fields must start at bit zero}}
func.func private @packed_union_offset(%arg: !obelisk_sim.packed_union<fields = [#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "b", type = i8, ordinal = 1, packedOffset = 8>], isTagged = false>)

// -----

// expected-error @+1 {{untagged packed union fields must have equal widths}}
func.func private @packed_union_width(%arg: !obelisk_sim.packed_union<fields = [#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "b", type = i16, ordinal = 1, packedOffset = 0>], isTagged = false>)

// -----

// expected-error @+1 {{packed tagged union requires 1 tag bits}}
func.func private @packed_union_tag_width(%arg: !obelisk_sim.packed_union<fields = [#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "b", type = i16, ordinal = 1, packedOffset = 0>], isTagged = true, tagBits = 2>)

// -----

// expected-error @+1 {{packed array element must be packed}}
func.func private @packed_unpacked_element(%arg: !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.unpacked_array<1 : 0 x i8>>)

// -----

func.func @bad_construct(%arg: i8) {
  // expected-error @+1 {{requires one operand per aggregate element}}
  %value = obelisk_sim.aggregate.construct %arg : (i8) -> !obelisk_sim.unpacked_array<1 : 0 x i8>
  return
}

// -----

func.func @bad_construct_type(%arg: i8, %other: i16) {
  // expected-error @+1 {{operand #1 does not match its aggregate element type}}
  %value = obelisk_sim.aggregate.construct %arg, %other : (i8, i16) -> !obelisk_sim.unpacked_array<1 : 0 x i8>
  return
}

// -----

func.func @bad_default() {
  // expected-error @+1 {{result must be a fixed aggregate type}}
  %value = obelisk_sim.aggregate.default : i8
  return
}

// -----

func.func @bad_extract(%arg: !obelisk_sim.unpacked_array<1 : 0 x i8>) {
  // expected-error @+1 {{aggregate index is out of range}}
  %value = obelisk_sim.aggregate.extract %arg[2] : (!obelisk_sim.unpacked_array<1 : 0 x i8>) -> i8
  return
}

// -----

func.func @bad_dynamic(%arg: !obelisk_sim.unpacked_struct<[#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>]>, %index: i32) {
  // expected-error @+1 {{input must be a fixed array}}
  %value = obelisk_sim.array.extract_dynamic %arg[%index] : (!obelisk_sim.unpacked_struct<[#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>]>, i32) -> i8
  return
}

// -----

func.func @bad_dynamic_result(%arg: !obelisk_sim.unpacked_array<1 : 0 x i8>, %index: i32) {
  // expected-error @+1 {{result must match the array element type}}
  %value = obelisk_sim.array.extract_dynamic %arg[%index] : (!obelisk_sim.unpacked_array<1 : 0 x i8>, i32) -> i16
  return
}

// -----

func.func @bad_dynamic_index(%arg: !obelisk_sim.unpacked_array<1 : 0 x i8>, %index: f32) {
  // expected-error @+1 {{index must be a signless builtin integer or four-state logic}}
  %value = obelisk_sim.array.extract_dynamic %arg[%index] : (!obelisk_sim.unpacked_array<1 : 0 x i8>, f32) -> i8
  return
}

// -----

func.func @bad_insert(%arg: !obelisk_sim.unpacked_array<1 : 0 x i8>, %replacement: i16) {
  // expected-error @+1 {{result type must match aggregate element type}}
  %value = obelisk_sim.aggregate.insert %replacement into %arg[0] : (!obelisk_sim.unpacked_array<1 : 0 x i8>, i16) -> !obelisk_sim.unpacked_array<1 : 0 x i8>
  return
}

// -----

func.func @bad_union_construct(%value: i16) {
  // expected-error @+1 {{result type must match aggregate element type}}
  %choice = obelisk_sim.union.construct %value as 0 : (i16) -> !obelisk_sim.unpacked_union<fields = [#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>], isTagged = false>
  return
}

// -----

func.func @bad_path(%arg: !obelisk_sim.ref<!obelisk_sim.unpacked_array<1 : 0 x i8>>) {
  // expected-error @+1 {{subelement path is out of range}}
  %value = obelisk_sim.ref.subelement %arg[[2]] : !obelisk_sim.ref<!obelisk_sim.unpacked_array<1 : 0 x i8>> -> !obelisk_sim.ref<i8>
  return
}

// -----

func.func @bad_path_type(%arg: !obelisk_sim.ref<!obelisk_sim.unpacked_array<1 : 0 x i8>>) {
  // expected-error @+1 {{result element type must match selected subelement}}
  %value = obelisk_sim.ref.subelement %arg[[0]] : !obelisk_sim.ref<!obelisk_sim.unpacked_array<1 : 0 x i8>> -> !obelisk_sim.ref<i16>
  return
}

// -----

func.func @bad_alloc(%arg: i8) {
  // expected-error @+1 {{initial value must match allocated element type}}
  %value = obelisk_sim.ref.alloc %arg : i8 -> !obelisk_sim.ref<i16>
  return
}

// -----

func.func @bad_flatten(%arg: !obelisk_sim.packed_array<1 : 0 x i8>) {
  // expected-error @+1 {{result must be the aggregate's width- and state-matched scalar}}
  %value = obelisk_sim.packed.flatten %arg : (!obelisk_sim.packed_array<1 : 0 x i8>) -> i8
  return
}

// -----

func.func @flatten_unpacked(%arg: !obelisk_sim.unpacked_array<1 : 0 x i8>) {
  // expected-error @+1 {{input must be a packed aggregate}}
  %value = obelisk_sim.packed.flatten %arg : (!obelisk_sim.unpacked_array<1 : 0 x i8>) -> i16
  return
}

// -----

// expected-error @+1 {{aggregate requires at least one field}}
func.func private @empty_aggregate(%arg: !obelisk_sim.unpacked_struct<[]>)

// -----

func.func private @bad_field_type(%arg: !obelisk_sim.unpacked_struct<[#obelisk_sim.field<name = "a", type = f32, ordinal = 0, packedOffset = 0>]>)

// -----

// expected-error @+1 {{fixed array range is too large}}
func.func private @oversized_array(%arg: !obelisk_sim.unpacked_array<9223372036854775807 : -9223372036854775808 x i8>)
