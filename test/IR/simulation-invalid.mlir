// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @duplicate {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{duplicate storage ID 0}}
    obelisk_sim.storage.decl 0 in 0 : i8 design
  }
}

// -----

module {
  func.func @bad_container_read(
      %array: !obelisk_sim.dynamic_array<i32>, %index: i64) {
    // expected-error @+1 {{result type must match the container element}}
    %value = "obelisk_sim.container.read"(%array, %index) :
      (!obelisk_sim.dynamic_array<i32>, i64) -> i64
    return
  }
}

// -----

module {
  func.func @bad_container_create(
      %array: !obelisk_sim.dynamic_array<i32>,
      %queue: !obelisk_sim.queue<i32, 4>, %size: i64) {
    // expected-error @+1 {{source and result container types must match}}
    %value = "obelisk_sim.container.create_like"(%array, %queue, %size) :
      (!obelisk_sim.dynamic_array<i32>, !obelisk_sim.queue<i32, 4>, i64) ->
      !obelisk_sim.dynamic_array<i32>
    return
  }
}

// -----

module {
  func.func @bad_assoc_create() {
    // expected-error @+1 {{element metadata does not match the associative element type}}
    %array = "obelisk_sim.assoc.create"() {
      type_id = 42 : i64, element_kind = 3 : i32,
      element_flags = 0 : i32, value_size = 8 : i64,
      alignment = 1 : i64, bit_width = 64 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      key_kind = 2 : i32, key_width = 32 : i64
    } : () -> !obelisk_sim.assoc_array<i32, i32, true, false>
    return
  }
}

// -----

module {
  func.func @bad_assoc_key(
      %array: !obelisk_sim.assoc_array<i32, i64, true, false>,
      %key: i64) {
    // expected-error @+1 {{key type must match the associative array key}}
    %value = "obelisk_sim.assoc.read"(%array, %key) :
      (!obelisk_sim.assoc_array<i32, i64, true, false>, i64) -> i64
    return
  }
}

// -----

module {
  func.func @missing_assoc_aggregate_trace() {
    // expected-error @+1 {{trace inventory does not match the associative element type}}
    %array = "obelisk_sim.assoc.create"() {
      type_id = 47 : i64, element_kind = 7 : i32,
      element_flags = 0 : i32, value_size = 16 : i64,
      alignment = 1 : i64, bit_width = 128 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      key_kind = 2 : i32, key_width = 32 : i64
    } : () -> !obelisk_sim.assoc_array<i32, !obelisk_sim.unpacked_struct<[
      #obelisk_sim.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
      #obelisk_sim.field<name = "text", type = !obelisk_sim.string, ordinal = 1, packedOffset = 0>
    ]>, true, false>
    return
  }
}

// -----

module {
  func.func @bad_assoc_traversal(
      %array: !obelisk_sim.assoc_array<i32, i64, true, false>,
      %key: i32) {
    // expected-error @+1 {{direction must be -1 or 1}}
    %next, %valid = "obelisk_sim.assoc.traverse"(%array, %key) {
      direction = 0 : i32, endpoint = false
    } : (!obelisk_sim.assoc_array<i32, i64, true, false>, i32) -> (i32, i1)
    return
  }
}

// -----

module {
  func.func @bad_typed_container_create(%size: i64) {
    // expected-error @+1 {{element metadata does not match the result container element type}}
    %array = "obelisk_sim.container.create"(%size) {
      type_id = 42 : i64, element_kind = 3 : i32,
      element_flags = 0 : i32, value_size = 8 : i64,
      alignment = 1 : i64, bit_width = 64 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      container_kind = 1 : i32, bound = 0 : i64
    } : (i64) -> !obelisk_sim.dynamic_array<i32>
    return
  }
}

// -----

module {
  func.func @bad_packed_aggregate_container_create(%size: i64) {
    // expected-error @+1 {{element metadata does not match the result container element type}}
    %array = "obelisk_sim.container.create"(%size) {
      type_id = 43 : i64, element_kind = 7 : i32,
      element_flags = 0 : i32, value_size = 1 : i64,
      alignment = 1 : i64, bit_width = 8 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      container_kind = 1 : i32, bound = 0 : i64
    } : (i64) ->
      !obelisk_sim.dynamic_array<!obelisk_sim.packed_array<7 : 0 x i1>>
    return
  }
}

// -----

module {
  func.func @missing_aggregate_trace(%size: i64) {
    // expected-error @+1 {{trace inventory does not match the result container element type}}
    %array = "obelisk_sim.container.create"(%size) {
      type_id = 44 : i64, element_kind = 7 : i32,
      element_flags = 0 : i32, value_size = 16 : i64,
      alignment = 1 : i64, bit_width = 128 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      container_kind = 1 : i32, bound = 0 : i64
    } : (i64) -> !obelisk_sim.dynamic_array<!obelisk_sim.unpacked_struct<[
      #obelisk_sim.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
      #obelisk_sim.field<name = "text", type = !obelisk_sim.string, ordinal = 1, packedOffset = 0>
    ]>>
    return
  }
}

// -----

module {
  func.func @wrong_aggregate_trace_kind(%size: i64) {
    // expected-error @+1 {{trace inventory does not match the result container element type}}
    %array = "obelisk_sim.container.create"(%size) {
      type_id = 45 : i64, element_kind = 7 : i32,
      element_flags = 0 : i32, value_size = 16 : i64,
      alignment = 1 : i64, bit_width = 128 : i64,
      trace_offsets = array<i64: 8>, trace_kinds = array<i32: 1>,
      container_kind = 1 : i32, bound = 0 : i64
    } : (i64) -> !obelisk_sim.dynamic_array<!obelisk_sim.unpacked_struct<[
      #obelisk_sim.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
      #obelisk_sim.field<name = "text", type = !obelisk_sim.string, ordinal = 1, packedOffset = 0>
    ]>>
    return
  }
}

// -----

module {
  func.func @wrong_aggregate_trace_offset(%size: i64) {
    // expected-error @+1 {{trace inventory does not match the result container element type}}
    %array = "obelisk_sim.container.create"(%size) {
      type_id = 46 : i64, element_kind = 7 : i32,
      element_flags = 0 : i32, value_size = 16 : i64,
      alignment = 1 : i64, bit_width = 128 : i64,
      trace_offsets = array<i64: 0>, trace_kinds = array<i32: 2>,
      container_kind = 1 : i32, bound = 0 : i64
    } : (i64) -> !obelisk_sim.dynamic_array<!obelisk_sim.unpacked_struct<[
      #obelisk_sim.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
      #obelisk_sim.field<name = "text", type = !obelisk_sim.string, ordinal = 1, packedOffset = 0>
    ]>>
    return
  }
}

// -----

module {
  obelisk_sim.design @conflicting_container_descriptors {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "first"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "second"
    obelisk_sim.func @first(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %size: i64 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %array = "obelisk_sim.container.create"(%size) {
        type_id = 99 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 4 : i64,
        alignment = 1 : i64, bit_width = 32 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<i32>
      obelisk_sim.return
    }
    obelisk_sim.func @second(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %size: i64 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      // expected-error @+1 {{element type ID 99 conflicts with another container descriptor}}
      %array = "obelisk_sim.container.create"(%size) {
        type_id = 99 : i64, element_kind = 3 : i32,
        element_flags = 0 : i32, value_size = 8 : i64,
        alignment = 1 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<f64>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @dpi_missing_status {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "dpi_missing_status"
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 1 : i32
      // expected-error @+1 {{must return a trailing runtime status}}
      %call = obelisk_sim.dpi.call "dpi_bad" id 1 scope 0 context %ctx : !obelisk_sim.context(%value) {abi_signature = [#obelisk_sim.dpi_abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>, #obelisk_sim.dpi_abi<kind = int, direction = result, width = 32, fourState = false, isSigned = true>], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "bad.sv", source_line = 1 : i32} : (i32) -> i32
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @dpi_bad_copyout {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "dpi_bad_copyout"
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 0 : i32
      // expected-error @+1 {{DPI formal copy-out must match its input ABI entry}}
      %call:2 = obelisk_sim.dpi.call "dpi_bad" id 1 scope 0 context %ctx : !obelisk_sim.context(%value) {abi_signature = [#obelisk_sim.dpi_abi<kind = int, direction = output, width = 32, fourState = false, isSigned = true>, #obelisk_sim.dpi_abi<kind = byte, direction = output, width = 8, fourState = false, isSigned = true>], is_context = false, is_pure = false, is_task = true, source_column = 1 : i32, source_file = "bad.sv", source_line = 1 : i32} : (i32) -> (i8, !obelisk_rt.status)
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @dpi_bad_result_order {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "dpi_bad_result_order"
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 1 : i32
      // expected-error @+1 {{a DPI function signature must place its result first}}
      %call:2 = obelisk_sim.dpi.call "dpi_bad" id 1 scope 0 context %ctx : !obelisk_sim.context(%value) {abi_signature = [#obelisk_sim.dpi_abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>, #obelisk_sim.dpi_abi<kind = int, direction = output, width = 32, fourState = false, isSigned = true>], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "bad.sv", source_line = 1 : i32} : (i32) -> (i32, !obelisk_rt.status)
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @dpi_bad_logical_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "dpi_bad_logical_width"
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 1 : i16
      // expected-error @+1 {{logical operand or result type disagrees with its DPI ABI entry}}
      %call:2 = obelisk_sim.dpi.call "dpi_bad" id 1 scope 0 context %ctx : !obelisk_sim.context(%value) {abi_signature = [#obelisk_sim.dpi_abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>, #obelisk_sim.dpi_abi<kind = int, direction = result, width = 32, fourState = false, isSigned = true>], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "bad.sv", source_line = 1 : i32} : (i16) -> (i32, !obelisk_rt.status)
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_connection_endpoint {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    // expected-error @+1 {{references an unknown scope or net descriptor}}
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
  }
}

// -----

module {
  obelisk_sim.design @out_of_range_connection {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<4> design
    // expected-error @+1 {{contains an out-of-range bit run}}
    obelisk_sim.net.connect.decl 0 in 0 0[3] to 1[0] width 2 reversed = false
  }
}

// -----

module {
  obelisk_sim.design @invalid_reversed_connection {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<4> design
    // expected-error @+1 {{contains an out-of-range bit run}}
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[1] width 3 reversed = true
  }
}

// -----

module {
  obelisk_sim.design @mixed_uwire_connection {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design {resolution_kind = 2 : i32}
    // expected-error @+1 {{mixes uwire with resolved wire/tri topology}}
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
  }
}

// -----

module {
  obelisk_sim.design @mixed_state_domain_connection {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : i1 design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    // expected-error @+1 {{connects incompatible two-state and four-state nets}}
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
  }
}

// -----

module {
  obelisk_sim.design @partial_driver_metadata {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    // expected-error @+1 {{driven low and width must either both be present or both be absent}}
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design {driven_low = 1 : i64}
  }
}

// -----

module {
  obelisk_sim.design @program_with_active_home {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "bad" debug "bad"
    // expected-error @+1 {{program-domain code units must have reactive home region}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, domain = 1 : i32, entry_kind = 1 : i32, home_region = 2 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @out_of_range_driver {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    // expected-error @+1 {{driven range exceeds the driver type}}
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design {driven_low = 3 : i64, driven_width = 2 : i64}
  }
}

// -----

module {
  // Only time-controlled statements are illegal in a SystemVerilog function.
  obelisk_sim.design @delay_function {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.delay_function.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @wait_children_function {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.wait_children_function.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      obelisk_sim.suspend.children to ^done
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_summary {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_summary.process.9000001"
    obelisk_sim.scope.decl 0
    // expected-error @below {{attribute 'effect_summary' failed to satisfy constraint: compute effect array}}
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, effect_summary = [0 : i32], code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  // expected-error @+1 {{scope IDs must be dense from zero; missing 1}}
  obelisk_sim.design @sparse {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 2 parent 0
  }
}

// -----

module {
  obelisk_sim.design @cyclic_scopes {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{parent scope ID must precede the child scope ID}}
    obelisk_sim.scope.decl 1 parent 2
    obelisk_sim.scope.decl 2 parent 1
  }
}

// -----

module {
  // expected-error @+1 {{time precision must be a positive femtosecond value}}
  obelisk_sim.design @bad_time_precision attributes {time_precision_fs = 0 : i64} {
    obelisk_sim.scope.decl 0
  }
}

// -----

module {
  obelisk_sim.design @bad_capture {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_capture.bad.9000001"
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{requires one argument metadata dictionary per argument}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_call {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.bad_call.callee.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.bad_call.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @callee(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i8 {obelisk_sim.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return %value : i8
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{operand and result types must match callee signature}}
      obelisk_sim.call @callee(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_width {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_width.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{value and unknown planes must match result width}}
      %bad = obelisk_sim.logic.constant 0 : i8, 0 : i4 : !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_shift_amount {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_shift_amount.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %amount = obelisk_sim.time.constant 1
      // expected-error @+1 {{shift amount must be an integer or four-state logic}}
      %shifted = obelisk_sim.logic.shift left %value by %amount : (!obelisk_sim.logic<8>, !obelisk_sim.time) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_dynamic_index {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_dynamic_index.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %index = obelisk_sim.time.constant 1
      // expected-error @+1 {{index must be a signless builtin integer or four-state logic}}
      %bad = obelisk_sim.logic.dyn_extract %value from %index : (!obelisk_sim.logic<8>, !obelisk_sim.time) -> !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_signed_dynamic_index {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_signed_dynamic_index.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    ^invalid(%index: si32):
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{builtin integer index must be signless}}
      %bad = obelisk_sim.logic.dyn_extract %value from %index : (!obelisk_sim.logic<8>, si32) -> !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_conversion_domain {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_conversion_domain.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{result must be a signless builtin integer}}
      %bad = obelisk_sim.logic.to_bits %value : !obelisk_sim.logic<8> -> si8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_dynamic_width {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_dynamic_width.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i4
      %index = arith.constant 0 : i32
      // expected-error @+1 {{result width exceeds input width}}
      %bad = obelisk_sim.bits.dyn_extract %value from %index : (i4, i32) -> i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_dynamic_domain {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_dynamic_domain.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %ref = obelisk_sim.ref.alloc %value : !obelisk_sim.logic<8> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %index = arith.constant 0 : i32
      // expected-error @+1 {{input and result element types must use the same state domain}}
      %bad = obelisk_sim.ref.dyn_extract %ref from %index : (!obelisk_sim.ref<!obelisk_sim.logic<8>>, i32) -> !obelisk_sim.ref<i4>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_logical_not_width {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_logical_not_width.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{logical negation must produce !obelisk_sim.logic<1>}}
      %bad = obelisk_sim.logic.unary logical_not %value : (!obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_selection_width {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_selection_width.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{constant selection is outside the input width}}
      %part = obelisk_sim.logic.extract %value from 6 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<4>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_continuation {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_continuation.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 0 : i8
      // expected-error @+1 {{type mismatch for bb argument #0 of successor #0}}
      obelisk_sim.suspend.change %ref to ^next(%value : i8) : !obelisk_sim.ref<i8>
    ^next(%wrong: i16):
      obelisk_sim.return
    }
  }
}

// -----

module {
  // expected-error @+1 {{Operations with a 'SymbolTable' must have exactly one block}}
  "obelisk_sim.design"() ({
  }) {sym_name = "empty"} : () -> ()
}

// -----

module {
  obelisk_sim.design @unknown_lookup {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.unknown_lookup.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{references an unknown or incompatible storage descriptor}}
      %ref = obelisk_sim.context.storage %ctx[7] : !obelisk_sim.ref<i8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @capture_descriptor_mismatch {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.capture_descriptor_mismatch.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{argument #1 has an incompatible capture descriptor}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %capture: !obelisk_sim.ref<i16> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @capture_subelement_metadata_mismatch {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.capture_subelement_metadata_mismatch.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<4>> design
    // Ordinal zero is the high packed element at physical offset four.
    // expected-error @+1 {{argument #1 has an incompatible capture descriptor}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %capture: !obelisk_sim.ref<!obelisk_sim.logic<4>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64, obelisk_sim.descriptor_root_type = !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<4>>, obelisk_sim.descriptor_low = 0 : i64, obelisk_sim.descriptor_indices = array<i64: 0>, obelisk_sim.descriptor_aggregate_type = !obelisk_sim.logic<4>}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @capture_packed_metadata_mismatch {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.capture_packed_metadata_mismatch.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    // expected-error @+1 {{argument #1 has an incompatible capture descriptor}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %capture: !obelisk_sim.ref<!obelisk_sim.logic<2>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64, obelisk_sim.descriptor_root_type = !obelisk_sim.logic<8>, obelisk_sim.descriptor_low = 3 : i64, obelisk_sim.descriptor_aggregate_type = !obelisk_sim.logic<8>, obelisk_sim.descriptor_packed_low = 2 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @blocking_function {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.blocking_function.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      obelisk_sim.suspend.delay %delay to ^next
    ^next:
      %zero = arith.constant 0 : i8
      obelisk_sim.return %zero : i8
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_any {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_any.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      // expected-error @+1 {{edge inventory exceeds the operand inventory}}
      obelisk_sim.suspend.any %ref edges [0, 1] to ^next : !obelisk_sim.ref<i8>
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @cross_isolation {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.cross_isolation.bad.9000001"
    obelisk_sim.scope.decl 0
    %outside = arith.constant 0 : i8
    // expected-note @+1 {{required by region isolation constraints}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{using value defined outside the region}}
      obelisk_sim.return %outside : i8
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_driver {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : i8 design
    // expected-error @+1 {{references an incompatible scope or net descriptor}}
    obelisk_sim.driver.decl 0 in 0 drives 0 : i16 design
  }
}

// -----

module {
  // expected-error @+1 {{design must contain a root scope descriptor}}
  obelisk_sim.design @no_root {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.no_root.bad.9000001"
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @two_roots {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{design must contain exactly one root scope}}
    obelisk_sim.scope.decl 1
  }
}

// -----

module {
  obelisk_sim.design @self_parent {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{scope cannot be its own parent}}
    obelisk_sim.scope.decl 1 parent 1
  }
}

// -----

module {
  obelisk_sim.design @unknown_storage_scope {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{references an unknown scope ID}}
    obelisk_sim.storage.decl 0 in 4 : i8 design
  }
}

// -----

module {
  obelisk_sim.design @unknown_net_scope {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{references an unknown scope ID}}
    obelisk_sim.net.decl 0 in 4 : i8 design
  }
}

// -----

module {
  obelisk_sim.design @unknown_callee {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.unknown_callee.caller.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{callee must name a sibling function entry}}
      obelisk_sim.call @missing(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @call_targets_process {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.call_targets_process.process.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.call_targets_process.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @process(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{callee must name a sibling function entry}}
      obelisk_sim.call @process(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @spawn_targets_function {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.spawn_targets_function.callee.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.spawn_targets_function.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @callee(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i8
      obelisk_sim.return %zero : i8
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{callee must name a sibling process entry}}
      %process = obelisk_sim.spawn @callee(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @spawn_signature {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.spawn_signature.process.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.spawn_signature.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @process(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i8 {obelisk_sim.capture_kind = 2 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{operands must match the void callee signature}}
      %process = obelisk_sim.spawn @process(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @missing_context {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.missing_context.bad.9000001"
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{first argument must be !obelisk_sim.context}}
    obelisk_sim.func @bad(%value: i8 {obelisk_sim.capture_kind = 2 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @process_returns_value {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.process_returns_value.bad.9000001"
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{process and root entries must not return values}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i8
      obelisk_sim.return %zero : i8
    }
  }
}

// -----

module {
  obelisk_sim.design @root_takes_captures {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{root initializer accepts only the context argument}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 0 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @context_capture_metadata {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.context_capture_metadata.bad.9000001"
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{argument #1 cannot have context capture metadata}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i8 {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @spurious_descriptor {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.spurious_descriptor.bad.9000001"
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{argument #1 must not have descriptor metadata}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i8 {obelisk_sim.capture_kind = 2 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @missing_descriptor {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.missing_descriptor.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{argument #1 requires obelisk_sim.descriptor_id metadata}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @return_type_mismatch {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.return_type_mismatch.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i16
      // expected-error @+1 {{operand types must match the enclosing function results}}
      obelisk_sim.return %zero : i16
    }
  }
}

// -----

module {
  obelisk_sim.design @watched_is_value {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.watched_is_value.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{watched value must be a ref or net handle}}
      obelisk_sim.suspend.change %value to ^next : i8
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @any_needs_edges {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.any_needs_edges.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      // expected-error @+1 {{requires at least one watched handle}}
      obelisk_sim.suspend.any %ref edges [] to ^next : !obelisk_sim.ref<i8>
    ^next(%resumed: !obelisk_sim.ref<i8>):
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @any_bad_edge {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.any_bad_edge.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      // expected-error @+1 {{contains an invalid edge kind}}
      obelisk_sim.suspend.any %ref edges [9] to ^next : !obelisk_sim.ref<i8>
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @alloc_mismatch {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.alloc_mismatch.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{initial value must match allocated element type}}
      %local = obelisk_sim.ref.alloc %value : i8 -> !obelisk_sim.ref<i16>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @concat_width {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.concat_width.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{result width must equal the sum of input widths}}
      %bad = obelisk_sim.logic.concat %value, %value : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @replicate_width {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.replicate_width.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{result width must equal input width times count}}
      %bad = obelisk_sim.logic.replicate %value times 3 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @replicate_width_overflow {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.replicate_width_overflow.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i4, 0 : i4 : !obelisk_sim.logic<4>
      // expected-error @+1 {{replication width overflows uint64_t}}
      %bad = obelisk_sim.logic.replicate %value times 4611686018427387905 : !obelisk_sim.logic<4> -> !obelisk_sim.logic<4>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @insert_range {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.insert_range.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %part = obelisk_sim.logic.constant 0 : i4, 0 : i4 : !obelisk_sim.logic<4>
      // expected-error @+1 {{replacement is outside the input width}}
      %bad = obelisk_sim.logic.insert %part into %value at 6 : (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @case_compare_result {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.case_compare_result.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{case comparisons must produce i1}}
      %bad = obelisk_sim.logic.compare case_eq %value, %value : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @wild_compare_result {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.wild_compare_result.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{four-state comparisons must produce !obelisk_sim.logic<1>}}
      %bad = obelisk_sim.logic.compare wild_eq %value, %value : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> i1
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @casez_compare_result {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.casez_compare_result.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{case comparisons must produce i1}}
      %bad = obelisk_sim.logic.compare casez_eq %value, %value : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @union_active_untagged {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.union_active_untagged.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      %union = obelisk_sim.union.construct %value as 0 : (i8) -> !obelisk_sim.unpacked_union<fields = [#obelisk_sim.field<name = "only", type = i8, ordinal = 0, packedOffset = 0>], isTagged = false>
      // expected-error @+1 {{input union must be tagged}}
      %bad = obelisk_sim.union.is_active %union[0] : !obelisk_sim.unpacked_union<fields = [#obelisk_sim.field<name = "only", type = i8, ordinal = 0, packedOffset = 0>], isTagged = false>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @union_active_index {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.union_active_index.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      %union = obelisk_sim.union.construct %value as 0 : (i8) -> !obelisk_sim.packed_union<fields = [#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "b", type = i8, ordinal = 1, packedOffset = 0>], isTagged = true, tagBits = 1>
      // expected-error @+1 {{tagged union member index is out of range}}
      %bad = obelisk_sim.union.is_active %union[2] : !obelisk_sim.packed_union<fields = [#obelisk_sim.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "b", type = i8, ordinal = 1, packedOffset = 0>], isTagged = true, tagBits = 1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @negative_time {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.negative_time.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{simulation time must be nonnegative}}
      %bad = obelisk_sim.time.constant -1
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @ref_extract_range {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.ref_extract_range.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      // expected-error @+1 {{constant selection is outside the input element width}}
      %bad = obelisk_sim.ref.extract %ref from 6 : !obelisk_sim.ref<i8> -> !obelisk_sim.ref<i4>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_time_scale {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_time_scale.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 1 : i64
      // expected-error @+1 {{tick scale must be positive}}
      %bad = obelisk_sim.time.scale %value by 0 signed = false : i64
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_time_to_real_scale {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_time_to_real_scale.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 1 : i64
      // expected-error @+1 {{tick scale must be positive}}
      %bad = obelisk_sim.time.to_real %value by 0
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_time_from_real_quantum {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_time_from_real_quantum.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 1.0 : f64
      // expected-error @+1 {{tick quantum must divide the tick scale}}
      %bad = obelisk_sim.time.from_real %value by 10 quantum 3
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_edge_iff_condition {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_edge_iff_condition.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 1 : i8
      // expected-error @+1 {{condition must be a ref or net handle}}
      obelisk_sim.suspend.edge_iff posedge %ref iff %value to ^next : !obelisk_sim.ref<i8>, i8
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_edge_iff_primary {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_edge_iff_primary.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      // expected-error @+1 {{primary event must request an edge}}
      obelisk_sim.suspend.edge_iff change %ref iff %ref to ^next : !obelisk_sim.ref<i8>, !obelisk_sim.ref<i8>
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_level_handle {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_level_handle.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 1 : i8
      // expected-error @+1 {{watched value must be a ref or net handle}}
      obelisk_sim.suspend.level %value to ^next : i8
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_join {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_join.child.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.bad_join.bad.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @child(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %process = obelisk_sim.spawn @child(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      // expected-error @+1 {{process count exceeds the operand inventory}}
      obelisk_sim.suspend.join all %process processes 2 to ^next : !obelisk_sim.process
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @empty_join {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.empty_join.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{requires at least one child process}}
      obelisk_sim.suspend.join all %value processes 0 to ^next : i8
    ^next(%continued: i8):
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @task_result {
    obelisk_sim.code_unit.decl 9000001 in 0 task hierarchy "test.task_result.bad.9000001"
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{process and root entries must not return values}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 12 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      obelisk_sim.return %value : i8
    }
  }
}

// -----

module {
  obelisk_sim.design @task_call_non_task {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.task_call_non_task.callee.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.task_call_non_task.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @callee(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{callee must name a sibling task entry}}
      obelisk_sim.task.call @callee(%ctx) arguments 1 to ^done : !obelisk_sim.context
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @task_call_signature {
    obelisk_sim.code_unit.decl 9000001 in 0 task hierarchy "test.task_call_signature.callee.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.task_call_signature.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @callee(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i8 {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{argument types must match the task signature}}
      obelisk_sim.task.call @callee(%ctx) arguments 1 to ^done : !obelisk_sim.context
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @task_call_function {
    obelisk_sim.code_unit.decl 9000001 in 0 task hierarchy "test.task_call_function.callee.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.task_call_function.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @callee(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      obelisk_sim.task.call @callee(%ctx) arguments 1 to ^done : !obelisk_sim.context
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @negative_control_enter {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.negative_control_enter.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{control target ID must be positive}}
      %control = obelisk_sim.control.enter 0
      obelisk_sim.control.leave %control
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @negative_control_disable {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.negative_control_disable.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{control target ID must be positive}}
      obelisk_sim.control.disable 0
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @hierarchical_activation_disable {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.hierarchical_activation_disable.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %control = obelisk_sim.control.enter 1
      // expected-error @+1 {{hierarchical disable must not name one activation token}}
      obelisk_sim.control.disable 1 activation %control {hierarchical = true}
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @negative_static_once {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.negative_static_once.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{static initialization ID must be positive}}
      %first = obelisk_sim.static.once 0
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_display_radix {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_display_radix.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{default radix must be 2, 8, 10, or 16}}
      obelisk_sim.display %ctx to %fd(%value) newline = true radix = 3 flags = [0] : i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_display_flags {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_display_flags.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{requires one flag entry per display item}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [] : i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_display_flag {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.unknown_display_flag.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{container display flags require a container operand}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [16] : i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @signed_literal_display {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.signed_literal_display.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %text = obelisk_sim.bytes.constant "text"
      // expected-error @+1 {{literal byte items cannot be signed}}
      obelisk_sim.display %ctx to %fd(%text) newline = false radix = 10 flags = [1] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unsupported_display_item {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.unsupported_display_item.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0.0 : f32
      // expected-error @+1 {{items must be literal bytes, packed integers, or f64 reals}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [0] : f32
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unmarked_real_display_item {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.unmarked_real_display_item.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0.0 : f64
      // expected-error @+1 {{f64 display operands must be marked real}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [0] : f64
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @signed_real_display_item {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.signed_real_display_item.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0.0 : f64
      // expected-error @+1 {{real display items cannot be marked signed}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [5] : f64
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_display_time_multiplier {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_display_time_multiplier.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{time multiplier must be positive}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [0] {time_multiplier = 0 : i64} : i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_code_unit {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 17 in 0 function hierarchy "top.first"
    // expected-error @+1 {{duplicate code-unit ID 17}}
    obelisk_sim.code_unit.decl 17 in 0 function hierarchy "top.second"
  }
}

// -----

module {
  obelisk_sim.design @unknown_code_unit_scope {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{references an unknown scope ID}}
    obelisk_sim.code_unit.decl 17 in 1 function hierarchy "top.missing"
  }
}

// -----

module {
  obelisk_sim.design @missing_code_unit_reference {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{defined non-root function requires a code-unit ID}}
    obelisk_sim.func @missing(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @zero_code_unit_id {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{code-unit ID must be nonzero}}
    obelisk_sim.code_unit.decl 0 in 0 function hierarchy "top.zero"
  }
}

// -----

module {
  obelisk_sim.design @duplicate_executable_code_unit_reference {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 17 in 0 function hierarchy "top.shared"
    obelisk_sim.func @first(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return
    }
    // expected-error @+1 {{code-unit ID 17 is referenced by multiple executable functions}}
    obelisk_sim.func @second(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return
    }
    // expected-remark @-11 {{first executable function is here}}
  }
}

// -----

module {
  obelisk_sim.design @mismatched_code_unit_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 17 in 0 initial hierarchy "top.initial"
    // expected-error @+1 {{entry kind does not match its code-unit declaration}}
    obelisk_sim.func @function(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @observer_bad_result {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_bad_result"
    // expected-error @+1 {{observer entry must return one scalar result}}
    obelisk_sim.func private @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> !obelisk_sim.time attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %zero = obelisk_sim.time.constant 0
      obelisk_sim.return %zero : !obelisk_sim.time
    }
  }
}

// -----

module {
  obelisk_sim.design @observer_suspends {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_suspends"
    obelisk_sim.func private @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{is not permitted in a zero-time observer entry}}
      obelisk_sim.suspend.forever to ^resume
    ^resume:
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }
  }
}

// -----

module {
  obelisk_sim.design @observer_calls_task {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9000001 in 0 task hierarchy "test.observer_calls_task.callee"
    obelisk_sim.code_unit.decl 9000002 in 0 observer hierarchy "test.observer_calls_task.bad"
    obelisk_sim.func private @callee(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func private @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{task calls are not permitted in an observer entry}}
      obelisk_sim.task.call @callee(%ctx) arguments 1 to ^resume : !obelisk_sim.context
    ^resume:
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }
  }
}

// -----

module {
  obelisk_sim.design @observer_bind_signature {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i16 design
    obelisk_sim.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_bind_signature.evaluator"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.observer_bind_signature.bad"
    obelisk_sim.func private @evaluator(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 2 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i16>
      // expected-error @+1 {{capture types must match evaluator arguments after context}}
      %bound = obelisk_sim.observer.bind @evaluator values(%ref, %ref : !obelisk_sim.ref<i16>, !obelisk_sim.ref<i16>) captures 1 : !obelisk_sim.observer<i1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @observer_bind_dependency {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_bind_dependency.evaluator"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.observer_bind_dependency.bad"
    obelisk_sim.func private @evaluator(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{dependencies must be storage, net, or named-event handles}}
      %bound = obelisk_sim.observer.bind @evaluator values(%value : i8) captures 0 : !obelisk_sim.observer<i1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @suspend_observe_primary_type {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.suspend_observe_primary_type.bad"
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      // expected-error @+1 {{primary operands must be observer handles}}
      obelisk_sim.suspend.observe %false, %false conditions 0 edges [0] indices [-1] to ^resume : i1, i1
    ^resume:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @suspend_observe_clauses {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.suspend_observe_clauses.bad"
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      // expected-error @+1 {{requires one initial value, edge, and condition index per primary}}
      obelisk_sim.suspend.observe %false conditions 0 edges [0, 1] indices [-1, -1] to ^resume : i1
    ^resume:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @observer_bind_capture_abi {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_bind_capture_abi.evaluator"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.observer_bind_capture_abi.bad"
    obelisk_sim.func private @evaluator(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %scalar: i8 {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %scalar = arith.constant 0 : i8
      // expected-error @+1 {{captures must use storage, net, driver, or named-event handles}}
      %bound = obelisk_sim.observer.bind @evaluator values(%scalar : i8) captures 1 : !obelisk_sim.observer<i1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @suspend_observe_truncated_condition {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 9000001 in 0 observer hierarchy "test.suspend_observe_truncated_condition.primary"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.suspend_observe_truncated_condition.bad"
    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %dependency = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %primary = obelisk_sim.observer.bind @primary values(%dependency : !obelisk_sim.ref<!obelisk_sim.logic<1>>) captures 0 : !obelisk_sim.observer<i1>
      %false = arith.constant false
      // expected-error @+1 {{condition count exceeds the operand inventory}}
      obelisk_sim.suspend.observe %primary, %false conditions 1 edges [0] indices [0] to ^resume : !obelisk_sim.observer<i1>, i1
    ^resume:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @frozen_two_state_unknown {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "bad"
    obelisk_sim.func private @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
          entry_kind = 8 : i32, code_unit_id = 1 : i64,
          obelisk_sim.bindings = [
            // expected-error @+2 {{two-state frozen constant must have a zero unknown plane}}
            // expected-error @+1 {{failed to parse SimConstantBindingAttr parameter 'value'}}
            #obelisk_sim.constant_binding<path = "P", value = #obelisk_sim.frozen_constant<value = [1 : i8, 1 : i8], isSigned = false> : i8>]
        } {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @binding_role_type {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "bad"
    // expected-error @below {{lvalue-only binding requires a storage, net, or driver argument}}
    obelisk_sim.func private @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {
          entry_kind = 8 : i32, code_unit_id = 1 : i64,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "value", argument = 1, kind = lvalue_only, copyOut = false>]
        } {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @binding_local_type {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "bad"
    obelisk_sim.func private @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
          entry_kind = 8 : i32, code_unit_id = 1 : i64,
          obelisk_sim.bindings = [
            // expected-error @+1 {{local binding type must be a normalized simulation value}}
            #obelisk_sim.local_binding<path = "local", type = !obelisk_sim.ref<i8>, automatic = false, patternVariable = false, isReturn = false>]
        } {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @binding_path_collision {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "bad"
    // expected-error @below {{both provide the source value for path 'same'}}
    obelisk_sim.func private @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {
          entry_kind = 1 : i32, code_unit_id = 1 : i64,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "same", argument = 1, kind = direct, copyOut = false>,
            #obelisk_sim.local_binding<path = "same", type = i32, automatic = false, patternVariable = false, isReturn = false>]
        } {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @binding_copy_out_pair {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 task hierarchy "bad"
    // expected-error @below {{copy-out destination path 'formal' requires a copy-out formal-local binding}}
    obelisk_sim.func private @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %destination: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32})
        attributes {
          entry_kind = 12 : i32, code_unit_id = 1 : i64,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "formal", argument = 1, kind = copy_out_destination, copyOut = false>]
        } {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @binding_multiple_returns {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "bad"
    // expected-error @below {{multiple local bindings are marked as the function return}}
    obelisk_sim.func private @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> i32
        attributes {
          entry_kind = 8 : i32, code_unit_id = 1 : i64,
          obelisk_sim.bindings = [
            #obelisk_sim.local_binding<path = "first", type = i32, automatic = false, patternVariable = false, isReturn = true>,
            #obelisk_sim.local_binding<path = "second", type = i32, automatic = false, patternVariable = false, isReturn = true>]
        } {
      %zero = arith.constant 0 : i32
      obelisk_sim.return %zero : i32
    }
  }
}

// -----

module {
  obelisk_sim.design @observed_delay {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
          entry_kind = 1 : i32, code_unit_id = 1 : i64,
          home_region = 8 : i32
        } {
      %zero = obelisk_sim.time.constant 0
      // expected-error @+1 {{is not permitted in an observed-region code unit}}
      obelisk_sim.suspend.delay %zero to ^resume
    ^resume:
      obelisk_sim.return
    }
  }
}

// -----

module {
  func.func @string_from_real(%value: f64) {
    // expected-error @+1 {{input must be a fixed packed value}}
    %string = obelisk_sim.string.from_packed %value :
      (f64) -> !obelisk_sim.string
    return
  }
}

// -----

module {
  func.func @bad_string_radix(%input: !obelisk_sim.string) {
    // expected-error @+1 {{radix must be 2, 8, 10, or 16}}
    %value = obelisk_sim.string.parse_integer %input radix = 3
    return
  }
}

// -----

module {
  obelisk_sim.design @invalid_deferred_assertion_site {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @+1 {{deferred assertion site ID must be positive}}
      %first = obelisk_sim.assert.deferred_once 0
      obelisk_sim.return
    }
  }
}
