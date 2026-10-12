// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  func.func @outside(%ctx: !simulation.context, %owner: !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) {
    // expected-error @+1 {{selected bit window is outside the owner layout}}
    %view = "simulation.reference_path.aggregate_slice"(%ctx, %owner) {
      alignment = 1 : i64,
      bit_offset = 1 : i64,
      bit_width = 32 : i64,
      element_flags = #simulation.element_flags<none>,
      element_kind = #simulation.element_kind<bits>,
      owner_bit_width = 32 : i64,
      owner_four_state = false,
      owner_plane_size = 4 : i64,
      trace_kinds = array<i32>,
      trace_offsets = array<i64>,
      type_id = 1 : i64,
      value_size = 4 : i64
    } : (!simulation.context, !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) -> !simulation.reference_path<i32>
    return
  }
}

// -----

module {
  func.func @overflow(%ctx: !simulation.context, %owner: !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) {
    // expected-error @+1 {{selected element size overflows the bit layout}}
    %view = "simulation.reference_path.aggregate_slice"(%ctx, %owner) {
      alignment = 1 : i64,
      bit_offset = 0 : i64,
      bit_width = 32 : i64,
      element_flags = #simulation.element_flags<none>,
      element_kind = #simulation.element_kind<bits>,
      owner_bit_width = 32 : i64,
      owner_four_state = false,
      owner_plane_size = 4 : i64,
      trace_kinds = array<i32>,
      trace_offsets = array<i64>,
      type_id = 1 : i64,
      value_size = 2305843009213693952 : i64
    } : (!simulation.context, !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) -> !simulation.reference_path<i32>
    return
  }
}

// -----

module {
  func.func @alignment(%ctx: !simulation.context, %owner: !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) {
    // expected-error @+1 {{selected element metadata is invalid}}
    %view = "simulation.reference_path.aggregate_slice"(%ctx, %owner) {
      alignment = 3 : i64,
      bit_offset = 0 : i64,
      bit_width = 32 : i64,
      element_flags = #simulation.element_flags<none>,
      element_kind = #simulation.element_kind<bits>,
      owner_bit_width = 32 : i64,
      owner_four_state = false,
      owner_plane_size = 4 : i64,
      trace_kinds = array<i32>,
      trace_offsets = array<i64>,
      type_id = 1 : i64,
      value_size = 6 : i64
    } : (!simulation.context, !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) -> !simulation.reference_path<i32>
    return
  }
}

// -----

module {
  func.func @width(%ctx: !simulation.context, %owner: !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) {
    // expected-error @+1 {{selected bit width must match the result element type}}
    %view = "simulation.reference_path.aggregate_slice"(%ctx, %owner) {
      alignment = 1 : i64,
      bit_offset = 0 : i64,
      bit_width = 31 : i64,
      element_flags = #simulation.element_flags<none>,
      element_kind = #simulation.element_kind<bits>,
      owner_bit_width = 32 : i64,
      owner_four_state = false,
      owner_plane_size = 4 : i64,
      trace_kinds = array<i32>,
      trace_offsets = array<i64>,
      type_id = 1 : i64,
      value_size = 4 : i64
    } : (!simulation.context, !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) -> !simulation.reference_path<i32>
    return
  }
}

// -----

module {
  func.func @unknown(%ctx: !simulation.context, %owner: !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) {
    // expected-error @+1 {{four-state selection requires an owner unknown plane}}
    %view = "simulation.reference_path.aggregate_slice"(%ctx, %owner) {
      alignment = 1 : i64,
      bit_offset = 0 : i64,
      bit_width = 32 : i64,
      element_flags = #simulation.element_flags<four_state>,
      element_kind = #simulation.element_kind<bits>,
      owner_bit_width = 32 : i64,
      owner_four_state = false,
      owner_plane_size = 4 : i64,
      trace_kinds = array<i32>,
      trace_offsets = array<i64>,
      type_id = 1 : i64,
      value_size = 4 : i64
    } : (!simulation.context, !simulation.argument_ref<!simulation.unpacked_array<0 : 0 x i32>>) -> !simulation.reference_path<i32>
    return
  }
}
