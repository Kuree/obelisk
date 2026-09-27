// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  simulation.design @duplicate {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{duplicate storage ID 0}}
    simulation.storage.decl 0 in 0 : i8 design
  }
}

// -----

module {
  simulation.design @bad_port_ordinal {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design hierarchy "top.a"
    // expected-error @+1 {{port ordinal must be an unsigned 24-bit integer}}
    simulation.port.decl 0 in 0 source 0 net = false at 0 : i8 input ordinal 16777216 hierarchy "top.a"
  }
}

// -----

module {
  simulation.design @bad_port_range {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design hierarchy "top.a"
    // expected-error @+1 {{references an incompatible scope or source descriptor}}
    simulation.port.decl 0 in 0 source 0 net = false at 7 : i2 input ordinal 0 hierarchy "top.a"
  }
}

// -----

!mixed_port_source = !simulation.packed_struct<[
  #simulation.field<name = "bits", type = i1, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "logic", type = !simulation.logic<1>, ordinal = 1, packedOffset = 1>
]>

module {
  // A fixed two-state member view is valid even though another member makes
  // the whole canonical storage descriptor four-state.
  simulation.design @mixed_port_representation {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !mixed_port_source design hierarchy "top.value"
    simulation.port.decl 0 in 0 source 0 net = false at 0 : i1 input ordinal 0 hierarchy "top.bits"
    // expected-error @+1 {{references an incompatible scope or source descriptor}}
    simulation.port.decl 1 in 0 source 0 net = false at 1 : i1 input ordinal 1 hierarchy "top.logic_as_bits"
  }
}

// -----

module {
  func.func @bad_dumpports_action(
      %ctx: !simulation.context, %path: !simulation.string, %value: i64) {
    // expected-error @+1 {{attribute 'action' failed to satisfy constraint: EVCD session control action}}
    simulation.dump.ports_control %ctx, %path, %value {action = 5 : i32} :
        (!simulation.context, !simulation.string, i64) -> ()
    return
  }
}

// -----

module {
  simulation.design @empty_descriptor_binding_path {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.code_unit.decl 9100001 in 0 function hierarchy "bad"
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          entry_kind = 8 : i32, code_unit_id = 9100001 : i64,
          simulation.bindings = [
            // expected-error @+1 {{descriptor binding path must not be empty}}
            #simulation.descriptor_binding<path = "", descriptor = 0, type = !simulation.ref<i8>>]
        } {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_descriptor_binding {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.code_unit.decl 9100002 in 0 function hierarchy "bad"
    // expected-error @+1 {{descriptor binding for path 'state' references an unknown or incompatible storage descriptor}}
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          entry_kind = 8 : i32, code_unit_id = 9100002 : i64,
          simulation.bindings = [
            #simulation.descriptor_binding<path = "state", descriptor = 1, type = !simulation.ref<i8>>]
        } {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @missing_implicit_constructor {
    // expected-error @+1 {{implicit constructor references an unknown function}}
    simulation.class.decl @C id 1 {implicit_constructor = @missing, is_abstract = false, is_final = false, is_interface = false}
  }
}

// -----

module {
  simulation.design @format_flag_on_integer {
    simulation.code_unit.decl 9100003 in 0 initial hierarchy "bad"
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9100003 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{packed integer items may only carry the signed flag}}
      simulation.display %ctx to %fd(%value) newline = false radix = <decimal> flags = [128] : i8
      simulation.return
    }
  }
}

// -----

module {
  func.func @bad_container_read(
      %array: !simulation.dynamic_array<i32>, %index: i64) {
    // expected-error @+1 {{result type must match the container element}}
    %value = "simulation.container.read"(%array, %index) :
      (!simulation.dynamic_array<i32>, i64) -> i64
    return
  }
}

// -----

module {
  func.func @aggregate_managed_nba(
      %value: !simulation.unpacked_struct<[
        #simulation.field<name = "text", type = !simulation.string, ordinal = 0, packedOffset = 0>]>,
      %destination: !simulation.ref<!simulation.unpacked_struct<[
        #simulation.field<name = "text", type = !simulation.string, ordinal = 0, packedOffset = 0>]>>) {
    // expected-error @+1 {{aggregate values containing managed handles are not yet supported}}
    simulation.nba.enqueue %value to %destination :
      (!simulation.unpacked_struct<[
        #simulation.field<name = "text", type = !simulation.string, ordinal = 0, packedOffset = 0>]>,
       !simulation.ref<!simulation.unpacked_struct<[
        #simulation.field<name = "text", type = !simulation.string, ordinal = 0, packedOffset = 0>]>>) -> ()
    return
  }
}

// -----

module {
  func.func @bad_random_cycle(%key: i64, %position: i64) {
    // expected-error @+1 {{width must be between 1 and 32 bits}}
    %next, %value = simulation.random.cycle_next %key, %position
      {width = 33 : i32} : (i64, i64) -> (i64, i64)
    return
  }
}

// -----

module {
  func.func @bad_container_create(
      %array: !simulation.dynamic_array<i32>,
      %queue: !simulation.queue<i32, 4>, %size: i64) {
    // expected-error @+1 {{source and result container types must match}}
    %value = "simulation.container.create_like"(%array, %queue, %size) :
      (!simulation.dynamic_array<i32>, !simulation.queue<i32, 4>, i64) ->
      !simulation.dynamic_array<i32>
    return
  }
}

// -----

module {
  func.func @bad_assoc_create() {
    // expected-error @+1 {{element metadata does not match the associative element type}}
    %array = "simulation.assoc.create"() {
      type_id = 42 : i64, element_kind = #simulation.element_kind<real>,
      element_flags = #simulation.element_flags<none>, value_size = 8 : i64,
      alignment = 1 : i64, bit_width = 64 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      key_kind = #simulation.assoc_key_kind<signed>, key_width = 32 : i64
    } : () -> !simulation.assoc_array<i32, i32, true, false>
    return
  }
}

// -----

module {
  func.func @bad_assoc_key(
      %array: !simulation.assoc_array<i32, i64, true, false>,
      %key: i64) {
    // expected-error @+1 {{key type must match the associative array key}}
    %value = "simulation.assoc.read"(%array, %key) :
      (!simulation.assoc_array<i32, i64, true, false>, i64) -> i64
    return
  }
}

// -----

module {
  func.func @missing_assoc_aggregate_trace() {
    // expected-error @+1 {{trace inventory does not match the associative element type}}
    %array = "simulation.assoc.create"() {
      type_id = 47 : i64, element_kind = #simulation.element_kind<aggregate>,
      element_flags = #simulation.element_flags<none>, value_size = 16 : i64,
      alignment = 1 : i64, bit_width = 128 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      key_kind = #simulation.assoc_key_kind<signed>, key_width = 32 : i64
    } : () -> !simulation.assoc_array<i32, !simulation.unpacked_struct<[
      #simulation.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
      #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>
    ]>, true, false>
    return
  }
}

// -----

module {
  func.func @bad_assoc_traversal(
      %array: !simulation.assoc_array<i32, i64, true, false>,
      %key: i32) {
    // expected-error @+1 {{attribute 'direction' failed to satisfy constraint}}
    %next, %valid = "simulation.assoc.traverse"(%array, %key) {
      direction = 0 : i32, endpoint = false
    } : (!simulation.assoc_array<i32, i64, true, false>, i32) -> (i32, i1)
    return
  }
}

// -----

module {
  func.func @bad_typed_container_create(%size: i64) {
    // expected-error @+1 {{element metadata does not match the result container element type}}
    %array = "simulation.container.create"(%size) {
      type_id = 42 : i64, element_kind = #simulation.element_kind<real>,
      element_flags = #simulation.element_flags<none>, value_size = 8 : i64,
      alignment = 1 : i64, bit_width = 64 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
    } : (i64) -> !simulation.dynamic_array<i32>
    return
  }
}

// -----

module {
  func.func @bad_packed_aggregate_container_create(%size: i64) {
    // expected-error @+1 {{element metadata does not match the result container element type}}
    %array = "simulation.container.create"(%size) {
      type_id = 43 : i64, element_kind = #simulation.element_kind<aggregate>,
      element_flags = #simulation.element_flags<none>, value_size = 1 : i64,
      alignment = 1 : i64, bit_width = 8 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
    } : (i64) ->
      !simulation.dynamic_array<!simulation.packed_array<7 : 0 x i1>>
    return
  }
}

// -----

module {
  func.func @missing_aggregate_trace(%size: i64) {
    // expected-error @+1 {{trace inventory does not match the result container element type}}
    %array = "simulation.container.create"(%size) {
      type_id = 44 : i64, element_kind = #simulation.element_kind<aggregate>,
      element_flags = #simulation.element_flags<none>, value_size = 16 : i64,
      alignment = 1 : i64, bit_width = 128 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
    } : (i64) -> !simulation.dynamic_array<!simulation.unpacked_struct<[
      #simulation.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
      #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>
    ]>>
    return
  }
}

// -----

module {
  func.func @wrong_aggregate_trace_kind(%size: i64) {
    // expected-error @+1 {{trace inventory does not match the result container element type}}
    %array = "simulation.container.create"(%size) {
      type_id = 45 : i64, element_kind = #simulation.element_kind<aggregate>,
      element_flags = #simulation.element_flags<none>, value_size = 16 : i64,
      alignment = 1 : i64, bit_width = 128 : i64,
      trace_offsets = array<i64: 8>, trace_kinds = array<i32: 1>,
      container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
    } : (i64) -> !simulation.dynamic_array<!simulation.unpacked_struct<[
      #simulation.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
      #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>
    ]>>
    return
  }
}

// -----

module {
  func.func @wrong_aggregate_trace_offset(%size: i64) {
    // expected-error @+1 {{trace inventory does not match the result container element type}}
    %array = "simulation.container.create"(%size) {
      type_id = 46 : i64, element_kind = #simulation.element_kind<aggregate>,
      element_flags = #simulation.element_flags<none>, value_size = 16 : i64,
      alignment = 1 : i64, bit_width = 128 : i64,
      trace_offsets = array<i64: 0>, trace_kinds = array<i32: 2>,
      container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
    } : (i64) -> !simulation.dynamic_array<!simulation.unpacked_struct<[
      #simulation.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
      #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>
    ]>>
    return
  }
}

// -----

module {
  simulation.design @conflicting_container_descriptors {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "first"
    simulation.code_unit.decl 2 in 0 initial hierarchy "second"
    simulation.func @first(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %size: i64 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %array = "simulation.container.create"(%size) {
        type_id = 99 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 4 : i64,
        alignment = 1 : i64, bit_width = 32 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<i32>
      simulation.return
    }
    simulation.func @second(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %size: i64 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      // expected-error @+1 {{element type ID 99 conflicts with another container descriptor}}
      %array = "simulation.container.create"(%size) {
        type_id = 99 : i64, element_kind = #simulation.element_kind<real>,
        element_flags = #simulation.element_flags<none>, value_size = 8 : i64,
        alignment = 1 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<f64>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @dpi_missing_status {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "dpi_missing_status"
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 1 : i32
      // expected-error @+1 {{must return a trailing runtime status}}
      %call = simulation.dpi.call "dpi_bad" id 1 scope 0 context %ctx : !simulation.context(%value) {abi_signature = [#simulation.dpi_abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>, #simulation.dpi_abi<kind = int, direction = result, width = 32, fourState = false, isSigned = true>], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "bad.sv", source_line = 1 : i32} : (i32) -> i32
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @dpi_bad_copyout {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "dpi_bad_copyout"
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 0 : i32
      // expected-error @+1 {{DPI formal copy-out must match its input ABI entry}}
      %call:2 = simulation.dpi.call "dpi_bad" id 1 scope 0 context %ctx : !simulation.context(%value) {abi_signature = [#simulation.dpi_abi<kind = int, direction = output, width = 32, fourState = false, isSigned = true>, #simulation.dpi_abi<kind = byte, direction = output, width = 8, fourState = false, isSigned = true>], is_context = false, is_pure = false, is_task = true, source_column = 1 : i32, source_file = "bad.sv", source_line = 1 : i32} : (i32) -> (i8, !runtime.status)
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @dpi_bad_result_order {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "dpi_bad_result_order"
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 1 : i32
      // expected-error @+1 {{a DPI function signature must place its result first}}
      %call:3 = simulation.dpi.call "dpi_bad" id 1 scope 0 context %ctx : !simulation.context(%value) {abi_signature = [#simulation.dpi_abi<kind = int, direction = output, width = 32, fourState = false, isSigned = true>, #simulation.dpi_abi<kind = int, direction = output, width = 32, fourState = false, isSigned = true>, #simulation.dpi_abi<kind = int, direction = result, width = 32, fourState = false, isSigned = true>], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "bad.sv", source_line = 1 : i32} : (i32) -> (i32, i32, !runtime.status)
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @dpi_bad_logical_width {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "dpi_bad_logical_width"
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 1 : i16
      // expected-error @+1 {{logical operand or result type disagrees with its DPI ABI entry}}
      %call:2 = simulation.dpi.call "dpi_bad" id 1 scope 0 context %ctx : !simulation.context(%value) {abi_signature = [#simulation.dpi_abi<kind = int, direction = input, width = 32, fourState = false, isSigned = true>, #simulation.dpi_abi<kind = int, direction = result, width = 32, fourState = false, isSigned = true>], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "bad.sv", source_line = 1 : i32} : (i16) -> (i32, !runtime.status)
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @dpi_bad_real_kind {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "dpi_bad_real_kind"
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 1.000000e+00 : f64
      // expected-error @+1 {{shortreal DPI ABI entry requires an f32 value}}
      %call:2 = simulation.dpi.call "dpi_bad" id 1 scope 0 context %ctx : !simulation.context(%value) {abi_signature = [#simulation.dpi_abi<kind = shortreal, direction = input, width = 32, fourState = false, isSigned = false>, #simulation.dpi_abi<kind = real, direction = result, width = 64, fourState = false, isSigned = false>], is_context = false, is_pure = false, is_task = false, source_column = 1 : i32, source_file = "bad.sv", source_line = 1 : i32} : (f64) -> (f64, !runtime.status)
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_connection_endpoint {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    // expected-error @+1 {{references an unknown scope or net descriptor}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
  }
}

// -----

module {
  simulation.design @out_of_range_connection {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.net.decl 1 in 0 : !simulation.logic<4> design
    // expected-error @+1 {{contains an out-of-range bit run}}
    simulation.net.connect.decl 0 in 0 0[3] to 1[0] width 2 reversed = false
  }
}

// -----

module {
  simulation.design @invalid_reversed_connection {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.net.decl 1 in 0 : !simulation.logic<4> design
    // expected-error @+1 {{contains an out-of-range bit run}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[1] width 3 reversed = true
  }
}

// -----

module {
  simulation.design @mixed_uwire_wrong_dominance {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {resolution_kind = 2 : i32}
    // expected-error @+1 {{identifies the wrong dominant endpoint for these net types}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = false
  }
}

// -----

module {
  simulation.design @mixed_wired_missing_dominance {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {resolution_kind = 3 : i32}
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {resolution_kind = 4 : i32}
    // expected-error @+1 {{must identify the dominant endpoint in mixed net topology}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
  }
}

// -----

module {
  simulation.design @wire_wired_wrong_dominance {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {resolution_kind = 3 : i32}
    // expected-error @+1 {{identifies the wrong dominant endpoint for these net types}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = false
  }
}

// -----

module {
  simulation.design @wire_tri0_wrong_dominance {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {resolution_kind = 5 : i32}
    // expected-error @+1 {{identifies the wrong dominant endpoint for these net types}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = false
  }
}

// -----

module {
  simulation.design @uwire_supply_wrong_dominance {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {resolution_kind = 2 : i32}
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {resolution_kind = 7 : i32}
    // expected-error @+1 {{identifies the wrong dominant endpoint for these net types}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = false
  }
}

// -----

module {
  simulation.design @wire_trireg_wrong_dominance {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {resolution_kind = 9 : i32}
    // expected-error @+1 {{identifies the wrong dominant endpoint for these net types}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = false
  }
}

// -----

module {
  simulation.design @trireg_pull_wrong_dominance {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {resolution_kind = 9 : i32}
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {resolution_kind = 5 : i32}
    // expected-error @+1 {{identifies the wrong dominant endpoint for these net types}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = false
  }
}

// -----

module {
  simulation.design @two_state_trireg {
    simulation.scope.decl 0
    // expected-error @+1 {{trireg nets require an entirely four-state type}}
    simulation.net.decl 0 in 0 : i1 design {resolution_kind = 9 : i32}
  }
}

// -----

module {
  simulation.design @charge_strength_on_wire {
    simulation.scope.decl 0
    // expected-error @+1 {{only trireg nets may declare charge strength}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {charge_strength = 2 : i32}
  }
}

// -----

module {
  simulation.design @invalid_trireg_charge_strength {
    simulation.scope.decl 0
    // expected-error @+1 {{trireg charge strength must be small, medium, or large}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {charge_strength = 6 : i32, resolution_kind = 9 : i32}
  }
}

// -----

module {
  simulation.design @mixed_state_domain_connection {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : i1 design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    // expected-error @+1 {{connects incompatible two-state and four-state nets}}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
  }
}

// -----

module {
  simulation.design @partial_driver_metadata {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    // expected-error @+1 {{driven low and width must either both be present or both be absent}}
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design {driven_low = 1 : i64}
  }
}

// -----

module {
  simulation.design @program_with_active_home {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "bad" debug "bad"
    // expected-error @+1 {{program-domain code units must have reactive home region}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, domain = 1 : i32, entry_kind = 1 : i32, home_region = 2 : i32} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @out_of_range_driver {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    // expected-error @+1 {{driven range exceeds the driver type}}
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design {driven_low = 3 : i64, driven_width = 2 : i64}
  }
}

// -----

module {
  // Only time-controlled statements are illegal in a SystemVerilog function.
  simulation.design @delay_function {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.delay_function.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %delay = simulation.time.constant 1
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @wait_children_function {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.wait_children_function.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      simulation.suspend.children to ^done
    ^done:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_summary {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_summary.process.9000001"
    simulation.scope.decl 0
    // expected-error @below {{attribute 'effect_summary' failed to satisfy constraint: compute effect array}}
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, effect_summary = [0 : i32], code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  // expected-error @+1 {{scope IDs must be dense from zero; missing 1}}
  simulation.design @sparse {
    simulation.scope.decl 0
    simulation.scope.decl 2 parent 0
  }
}

// -----

module {
  simulation.design @cyclic_scopes {
    simulation.scope.decl 0
    // expected-error @+1 {{parent scope ID must precede the child scope ID}}
    simulation.scope.decl 1 parent 2
    simulation.scope.decl 2 parent 1
  }
}

// -----

module {
  // expected-error @+1 {{time precision must be a positive femtosecond value}}
  simulation.design @bad_time_precision attributes {time_precision_fs = 0 : i64} {
    simulation.scope.decl 0
  }
}

// -----

module {
  simulation.design @bad_capture {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_capture.bad.9000001"
    simulation.scope.decl 0
    // expected-error @+1 {{requires one argument metadata dictionary per argument}}
    simulation.func @bad(%ctx: !simulation.context) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_call {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.bad_call.callee.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.bad_call.caller.9000002"
    simulation.scope.decl 0
    simulation.func @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      simulation.return %value : i8
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{operand and result types must match callee signature}}
      simulation.call @callee(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_width {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_width.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{value and unknown planes must match result width}}
      %bad = simulation.logic.constant 0 : i8, 0 : i4 : !simulation.logic<8>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_shift_amount {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_shift_amount.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %amount = simulation.time.constant 1
      // expected-error @+1 {{shift amount must be an integer or four-state logic}}
      %shifted = simulation.logic.shift left %value by %amount : (!simulation.logic<8>, !simulation.time) -> !simulation.logic<8>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_dynamic_index {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_dynamic_index.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %index = simulation.time.constant 1
      // expected-error @+1 {{index must be a signless builtin integer or four-state logic}}
      %bad = simulation.logic.dyn_extract %value from %index : (!simulation.logic<8>, !simulation.time) -> !simulation.logic<1>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_signed_dynamic_index {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_signed_dynamic_index.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    ^invalid(%index: si32):
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      // expected-error @+1 {{builtin integer index must be signless}}
      %bad = simulation.logic.dyn_extract %value from %index : (!simulation.logic<8>, si32) -> !simulation.logic<1>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_conversion_domain {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_conversion_domain.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      // expected-error @+1 {{result must be a signless builtin integer}}
      %bad = simulation.logic.to_bits %value : !simulation.logic<8> -> si8
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_dynamic_width {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_dynamic_width.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i4
      %index = arith.constant 0 : i32
      // expected-error @+1 {{result width exceeds input width}}
      %bad = simulation.bits.dyn_extract %value from %index : (i4, i32) -> i8
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_dynamic_domain {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_dynamic_domain.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %ref = simulation.ref.alloc %value : !simulation.logic<8> -> !simulation.ref<!simulation.logic<8>>
      %index = arith.constant 0 : i32
      // expected-error @+1 {{input and result element types must use the same state domain}}
      %bad = simulation.ref.dyn_extract %ref from %index : (!simulation.ref<!simulation.logic<8>>, i32) -> !simulation.ref<i4>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_dynamic_insert_width {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_dynamic_insert_width.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      %replacement = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %index = arith.constant 0 : i32
      // expected-error @+1 {{replacement width exceeds input width}}
      %bad = simulation.logic.dyn_insert %replacement into %value at %index : (!simulation.logic<4>, !simulation.logic<8>, i32) -> !simulation.logic<4>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_bits_dynamic_insert_domain {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_bits_dynamic_insert_domain.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %raw_value = arith.constant 0 : i8
      %raw_replacement = arith.constant 0 : i4
      %value = builtin.unrealized_conversion_cast %raw_value : i8 to si8
      %replacement = builtin.unrealized_conversion_cast %raw_replacement : i4 to si4
      %index = arith.constant 0 : i32
      // expected-error @+1 {{input, replacement, and result must be signless builtin integers}}
      %bad = simulation.bits.dyn_insert %replacement into %value at %index : (si8, si4, i32) -> si8
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_logical_not_width {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_logical_not_width.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      // expected-error @+1 {{logical negation must produce !simulation.logic<1>}}
      %bad = simulation.logic.unary logical_not %value : (!simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_selection_width {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_selection_width.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      // expected-error @+1 {{constant selection is outside the input width}}
      %part = simulation.logic.extract %value from 6 : !simulation.logic<8> -> !simulation.logic<4>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_continuation {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_continuation.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = arith.constant 0 : i8
      // expected-error @+1 {{type mismatch for bb argument #0 of successor #0}}
      simulation.suspend.change %ref to ^next(%value : i8) : !simulation.ref<i8>
    ^next(%wrong: i16):
      simulation.return
    }
  }
}

// -----

module {
  // expected-error @+1 {{Operations with a 'SymbolTable' must have exactly one block}}
  "simulation.design"() ({
  }) {sym_name = "empty"} : () -> ()
}

// -----

module {
  simulation.design @unknown_lookup {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.unknown_lookup.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{references an unknown or incompatible storage descriptor}}
      %ref = simulation.context.storage %ctx[7] : !simulation.ref<i8>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @capture_descriptor_mismatch {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.capture_descriptor_mismatch.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{argument #1 has an incompatible capture descriptor}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %capture: !simulation.ref<i16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @capture_subelement_metadata_mismatch {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.capture_subelement_metadata_mismatch.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<1 : 0 x !simulation.logic<4>> design
    // Ordinal zero is the high packed element at physical offset four.
    // expected-error @+1 {{argument #1 has an incompatible capture descriptor}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %capture: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64, simulation.descriptor_root_type = !simulation.packed_array<1 : 0 x !simulation.logic<4>>, simulation.descriptor_low = 0 : i64, simulation.descriptor_indices = array<i64: 0>, simulation.descriptor_aggregate_type = !simulation.logic<4>}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @capture_packed_metadata_mismatch {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.capture_packed_metadata_mismatch.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    // expected-error @+1 {{argument #1 has an incompatible capture descriptor}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %capture: !simulation.ref<!simulation.logic<2>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64, simulation.descriptor_root_type = !simulation.logic<8>, simulation.descriptor_low = 3 : i64, simulation.descriptor_aggregate_type = !simulation.logic<8>, simulation.descriptor_packed_low = 2 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @blocking_function {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.blocking_function.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %delay = simulation.time.constant 1
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      simulation.suspend.delay %delay to ^next
    ^next:
      %zero = arith.constant 0 : i8
      simulation.return %zero : i8
    }
  }
}

// -----

module {
  simulation.design @bad_any {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_any.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      // expected-error @+1 {{edge inventory exceeds the operand inventory}}
      simulation.suspend.any %ref edges [0, 1] to ^next : !simulation.ref<i8>
    ^next:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @cross_isolation {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.cross_isolation.bad.9000001"
    simulation.scope.decl 0
    %outside = arith.constant 0 : i8
    // expected-note @+1 {{required by region isolation constraints}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{using value defined outside the region}}
      simulation.return %outside : i8
    }
  }
}

// -----

module {
  simulation.design @bad_driver {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : i8 design
    // expected-error @+1 {{references an incompatible scope or net descriptor}}
    simulation.driver.decl 0 in 0 drives 0 : i16 design
  }
}

// -----

module {
  // expected-error @+1 {{design must contain a root scope descriptor}}
  simulation.design @no_root {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.no_root.bad.9000001"
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @two_roots {
    simulation.scope.decl 0
    // expected-error @+1 {{design must contain exactly one root scope}}
    simulation.scope.decl 1
  }
}

// -----

module {
  simulation.design @self_parent {
    simulation.scope.decl 0
    // expected-error @+1 {{scope cannot be its own parent}}
    simulation.scope.decl 1 parent 1
  }
}

// -----

module {
  simulation.design @empty_scope_definition {
    // expected-error @+1 {{module definition name cannot be empty}}
    simulation.scope.decl 0 source_definition ""
  }
}

// -----

module {
  simulation.design @root_scope_definition {
    // expected-error @+1 {{root scope cannot carry a module definition name}}
    simulation.scope.decl 0 source_definition "top"
  }
}

// -----

module {
  simulation.design @interface_scope_definition {
    simulation.scope.decl 0
    // expected-error @+1 {{only module scopes may carry a module definition name}}
    simulation.scope.decl 1 parent 0 source_definition "I" vpi_kind 601
  }
}

// -----

module {
  simulation.design @program_scope_definition {
    simulation.scope.decl 0
    // expected-error @+1 {{only module scopes may carry a module definition name}}
    simulation.scope.decl 1 parent 0 source_definition "P" vpi_kind 602
  }
}

// -----

module {
  simulation.design @zero_coverage_scope_id {
    // expected-error @+1 {{coverage scope ID must be nonzero}}
    simulation.scope.decl 0 coverage_id 0
  }
}

// -----

module {
  simulation.design @duplicate_coverage_scope_id {
    simulation.scope.decl 0 coverage_id 42
    // expected-error @+1 {{duplicate coverage scope ID 42}}
    simulation.scope.decl 1 parent 0 coverage_id 42
  }
}

// -----

module {
  simulation.design @unknown_storage_scope {
    simulation.scope.decl 0
    // expected-error @+1 {{references an unknown scope ID}}
    simulation.storage.decl 0 in 4 : i8 design
  }
}

// -----

module {
  simulation.design @unknown_net_scope {
    simulation.scope.decl 0
    // expected-error @+1 {{references an unknown scope ID}}
    simulation.net.decl 0 in 4 : i8 design
  }
}

// -----

module {
  simulation.design @unknown_callee {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.unknown_callee.caller.9000001"
    simulation.scope.decl 0
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{callee must name a sibling function or observer entry}}
      simulation.call @missing(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @call_targets_process {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.call_targets_process.process.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.call_targets_process.caller.9000002"
    simulation.scope.decl 0
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{callee must name a sibling function or observer entry}}
      simulation.call @process(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @spawn_targets_function {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.spawn_targets_function.callee.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.spawn_targets_function.caller.9000002"
    simulation.scope.decl 0
    simulation.func @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i8
      simulation.return %zero : i8
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{callee must name a sibling process entry}}
      %process = simulation.spawn @callee(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @spawn_signature {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.spawn_signature.process.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.spawn_signature.caller.9000002"
    simulation.scope.decl 0
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{operands must match the void callee signature}}
      %process = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @missing_context {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.missing_context.bad.9000001"
    simulation.scope.decl 0
    // expected-error @+1 {{first argument must be !simulation.context}}
    simulation.func @bad(%value: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @process_returns_value {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.process_returns_value.bad.9000001"
    simulation.scope.decl 0
    // expected-error @+1 {{process and root entries must not return values}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i8
      simulation.return %zero : i8
    }
  }
}

// -----

module {
  simulation.design @root_takes_captures {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{root initializer accepts only the context argument}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 0 : i32} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @context_capture_metadata {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.context_capture_metadata.bad.9000001"
    simulation.scope.decl 0
    // expected-error @+1 {{argument #1 cannot have context capture metadata}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i8 {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @spurious_descriptor {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.spurious_descriptor.bad.9000001"
    simulation.scope.decl 0
    // expected-error @+1 {{argument #1 must not have descriptor metadata}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i8 {simulation.capture_kind = 2 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @missing_descriptor {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.missing_descriptor.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{argument #1 requires simulation.descriptor_id metadata}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i8> {simulation.capture_kind = 3 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @return_type_mismatch {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.return_type_mismatch.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i16
      // expected-error @+1 {{operand types must match the enclosing function results}}
      simulation.return %zero : i16
    }
  }
}

// -----

module {
  simulation.design @watched_is_value {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.watched_is_value.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{watched value must be a ref, net, driver, or managed-watch handle}}
      simulation.suspend.change %value to ^next : i8
    ^next:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @any_needs_edges {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.any_needs_edges.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      // expected-error @+1 {{requires at least one watched handle}}
      simulation.suspend.any %ref edges [] to ^next : !simulation.ref<i8>
    ^next(%resumed: !simulation.ref<i8>):
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @any_bad_edge {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.any_bad_edge.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      // expected-error @+1 {{contains an invalid edge kind}}
      simulation.suspend.any %ref edges [9] to ^next : !simulation.ref<i8>
    ^next:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @alloc_mismatch {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.alloc_mismatch.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{initial value must match allocated element type}}
      %local = simulation.ref.alloc %value : i8 -> !simulation.ref<i16>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @concat_width {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.concat_width.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      // expected-error @+1 {{result width must equal the sum of input widths}}
      %bad = simulation.logic.concat %value, %value : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @replicate_width {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.replicate_width.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      // expected-error @+1 {{result width must equal input width times count}}
      %bad = simulation.logic.replicate %value times 3 : !simulation.logic<8> -> !simulation.logic<16>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @replicate_width_overflow {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.replicate_width_overflow.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      // expected-error @+1 {{replication width overflows uint64_t}}
      %bad = simulation.logic.replicate %value times 4611686018427387905 : !simulation.logic<4> -> !simulation.logic<4>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @insert_range {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.insert_range.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %part = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      // expected-error @+1 {{replacement is outside the input width}}
      %bad = simulation.logic.insert %part into %value at 6 : (!simulation.logic<8>, !simulation.logic<4>) -> !simulation.logic<8>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @case_compare_result {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.case_compare_result.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      // expected-error @+1 {{case comparisons must produce i1}}
      %bad = simulation.logic.compare case_eq %value, %value : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @wild_compare_result {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.wild_compare_result.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      // expected-error @+1 {{four-state comparisons must produce !simulation.logic<1>}}
      %bad = simulation.logic.compare wild_eq %value, %value : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @casez_compare_result {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.casez_compare_result.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      // expected-error @+1 {{case comparisons must produce i1}}
      %bad = simulation.logic.compare casez_eq %value, %value : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @union_active_untagged {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.union_active_untagged.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      %union = simulation.union.construct %value as 0 : (i8) -> !simulation.unpacked_union<fields = [#simulation.field<name = "only", type = i8, ordinal = 0, packedOffset = 0>], isTagged = false>
      // expected-error @+1 {{input union must be tagged}}
      %bad = simulation.union.is_active %union[0] : !simulation.unpacked_union<fields = [#simulation.field<name = "only", type = i8, ordinal = 0, packedOffset = 0>], isTagged = false>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @union_active_index {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.union_active_index.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      %union = simulation.union.construct %value as 0 : (i8) -> !simulation.packed_union<fields = [#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i8, ordinal = 1, packedOffset = 0>], isTagged = true, tagBits = 1>
      // expected-error @+1 {{tagged union member index is out of range}}
      %bad = simulation.union.is_active %union[2] : !simulation.packed_union<fields = [#simulation.field<name = "a", type = i8, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i8, ordinal = 1, packedOffset = 0>], isTagged = true, tagBits = 1>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @negative_time {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.negative_time.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{simulation time must be nonnegative}}
      %bad = simulation.time.constant -1
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @ref_extract_range {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.ref_extract_range.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      // expected-error @+1 {{constant selection is outside the input element width}}
      %bad = simulation.ref.extract %ref from 6 : !simulation.ref<i8> -> !simulation.ref<i4>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_time_scale {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_time_scale.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 1 : i64
      // expected-error @+1 {{tick scale must be positive}}
      %bad = simulation.time.scale %value by 0 signed = false : i64
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_time_to_real_scale {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_time_to_real_scale.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 1 : i64
      // expected-error @+1 {{tick scale must be positive}}
      %bad = simulation.time.to_real %value by 0
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_time_from_real_quantum {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_time_from_real_quantum.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 1.0 : f64
      // expected-error @+1 {{tick quantum must divide the tick scale}}
      %bad = simulation.time.from_real %value by 10 quantum 3
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_edge_iff_condition {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_edge_iff_condition.bad.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = arith.constant 1 : i8
      // expected-error @+1 {{condition must be a ref or net handle}}
      simulation.suspend.edge_iff posedge %ref iff %value to ^next : !simulation.ref<i8>, i8
    ^next:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_level_handle {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_level_handle.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 1 : i8
      // expected-error @+1 {{watched value must be a ref or net handle}}
      simulation.suspend.level %value to ^next : i8
    ^next:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_join {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_join.child.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.bad_join.bad.9000002"
    simulation.scope.decl 0
    simulation.func @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %process = simulation.spawn @child(%ctx) : !simulation.context -> !simulation.process
      // expected-error @+1 {{process count exceeds the operand inventory}}
      simulation.suspend.join all %process processes 2 to ^next : !simulation.process
    ^next:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @empty_join {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.empty_join.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{requires at least one child process}}
      simulation.suspend.join all %value processes 0 to ^next : i8
    ^next(%continued: i8):
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @task_result {
    simulation.code_unit.decl 9000001 in 0 task hierarchy "test.task_result.bad.9000001"
    simulation.scope.decl 0
    // expected-error @+1 {{process and root entries must not return values}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 12 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      simulation.return %value : i8
    }
  }
}

// -----

module {
  simulation.design @task_call_non_task {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.task_call_non_task.callee.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.task_call_non_task.caller.9000002"
    simulation.scope.decl 0
    simulation.func @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{callee must name a sibling task entry}}
      simulation.task.call @callee(%ctx) arguments 1 to ^done : !simulation.context
    ^done:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @task_call_signature {
    simulation.code_unit.decl 9000001 in 0 task hierarchy "test.task_call_signature.callee.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.task_call_signature.caller.9000002"
    simulation.scope.decl 0
    simulation.func @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i8 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{argument types must match the task signature}}
      simulation.task.call @callee(%ctx) arguments 1 to ^done : !simulation.context
    ^done:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @task_call_function {
    simulation.code_unit.decl 9000001 in 0 task hierarchy "test.task_call_function.callee.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.task_call_function.caller.9000002"
    simulation.scope.decl 0
    simulation.func @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      simulation.task.call @callee(%ctx) arguments 1 to ^done : !simulation.context
    ^done:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @negative_control_enter {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.negative_control_enter.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{control target ID must be positive}}
      %control = simulation.control.enter 0
      simulation.control.leave %control
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @negative_control_disable {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.negative_control_disable.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{control target ID must be positive}}
      simulation.control.disable 0
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @hierarchical_activation_disable {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.hierarchical_activation_disable.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %control = simulation.control.enter 1
      // expected-error @+1 {{hierarchical disable must not name one activation token}}
      simulation.control.disable 1 activation %control {hierarchical = true}
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @negative_static_once {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.negative_static_once.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{static initialization ID must be positive}}
      %first = simulation.static.once 0
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_display_radix {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_display_radix.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{expected '<'}}
      simulation.display %ctx to %fd(%value) newline = true radix = 3 flags = [0] : i8
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_display_flags {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_display_flags.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{requires one flag entry per display item}}
      simulation.display %ctx to %fd(%value) newline = false radix = <decimal> flags = [] : i8
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_display_flag {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.unknown_display_flag.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{container display flags require a container operand}}
      simulation.display %ctx to %fd(%value) newline = false radix = <decimal> flags = [16] : i8
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @signed_literal_display {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.signed_literal_display.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %text = simulation.bytes.constant "text"
      // expected-error @+1 {{literal byte items cannot be signed}}
      simulation.display %ctx to %fd(%text) newline = false radix = <decimal> flags = [1] : !simulation.bytes
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @virtual_interface_flag_on_integer {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.virtual_interface_flag_on_integer.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i64
      // expected-error @+1 {{virtual-interface display flags require a virtual-interface operand}}
      simulation.display %ctx to %fd(%value) newline = false radix = <decimal> flags = [256] : i64
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unmarked_virtual_interface {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.unmarked_virtual_interface.bad.9000001"
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0 interface "@bus"
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = simulation.virtual_interface.null : !simulation.virtual_interface<"@bus", "">
      // expected-error @+1 {{virtual-interface items require only the virtual-interface flag}}
      simulation.display %ctx to %fd(%value) newline = false radix = <decimal> flags = [0] : !simulation.virtual_interface<"@bus", "">
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @designated_literal_display {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.designated_literal_display.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %text = simulation.bytes.constant "text"
      // expected-error @+1 {{designated format must be the first string output-format item}}
      simulation.display %ctx to %fd(%text) newline = false radix = <decimal> flags = [32] : !simulation.bytes
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @late_designated_string_format {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.late_designated_string_format.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = arith.constant 0 : i8
      %text = simulation.bytes.constant "%d"
      // expected-error @+1 {{designated format must be the first string output-format item}}
      %result = simulation.string.output_format %ctx(%value, %text) radix = <decimal> flags = [0, 32] : i8, !simulation.bytes
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unsupported_display_item {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.unsupported_display_item.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0.0 : f32
      // expected-error @+1 {{items must be literal bytes, packed integers, or f64 reals}}
      simulation.display %ctx to %fd(%value) newline = false radix = <decimal> flags = [0] : f32
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unmarked_real_display_item {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.unmarked_real_display_item.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0.0 : f64
      // expected-error @+1 {{f64 display operands must be marked real}}
      simulation.display %ctx to %fd(%value) newline = false radix = <decimal> flags = [0] : f64
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @signed_real_display_item {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.signed_real_display_item.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0.0 : f64
      // expected-error @+1 {{real display items cannot be marked signed}}
      simulation.display %ctx to %fd(%value) newline = false radix = <decimal> flags = [5] : f64
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_display_time_multiplier {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.bad_display_time_multiplier.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{time multiplier must be positive}}
      simulation.display %ctx to %fd(%value) newline = false radix = <decimal> flags = [0] {time_multiplier = 0 : i64} : i8
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @duplicate_code_unit {
    simulation.scope.decl 0
    simulation.code_unit.decl 17 in 0 function hierarchy "top.first"
    // expected-error @+1 {{duplicate code-unit ID 17}}
    simulation.code_unit.decl 17 in 0 function hierarchy "top.second"
  }
}

// -----

module {
  simulation.design @unknown_code_unit_scope {
    simulation.scope.decl 0
    // expected-error @+1 {{references an unknown scope ID}}
    simulation.code_unit.decl 17 in 1 function hierarchy "top.missing"
  }
}

// -----

module {
  simulation.design @missing_code_unit_reference {
    simulation.scope.decl 0
    // expected-error @+1 {{defined non-root function requires a code-unit ID}}
    simulation.func @missing(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @zero_code_unit_id {
    simulation.scope.decl 0
    // expected-error @+1 {{code-unit ID must be nonzero}}
    simulation.code_unit.decl 0 in 0 function hierarchy "top.zero"
  }
}

// -----

module {
  simulation.design @duplicate_executable_code_unit_reference {
    simulation.scope.decl 0
    simulation.code_unit.decl 17 in 0 function hierarchy "top.shared"
    simulation.func @first(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
    // expected-error @+1 {{code-unit ID 17 is referenced by multiple executable functions}}
    simulation.func @second(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
    // expected-remark @-11 {{first executable function is here}}
  }
}

// -----

module {
  simulation.design @mismatched_code_unit_kind {
    simulation.scope.decl 0
    simulation.code_unit.decl 17 in 0 initial hierarchy "top.initial"
    // expected-error @+1 {{entry kind does not match its code-unit declaration}}
    simulation.func @function(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @observer_bad_result {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_bad_result"
    // expected-error @+1 {{observer entry must return one scalar result}}
    simulation.func private @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !simulation.time attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %zero = simulation.time.constant 0
      simulation.return %zero : !simulation.time
    }
  }
}

// -----

module {
  simulation.design @observer_suspends {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_suspends"
    simulation.func private @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{is not permitted in a zero-time observer entry}}
      simulation.suspend.forever to ^resume
    ^resume:
      %false = arith.constant false
      simulation.return %false : i1
    }
  }
}

// -----

module {
  simulation.design @observer_calls_task {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000001 in 0 task hierarchy "test.observer_calls_task.callee"
    simulation.code_unit.decl 9000002 in 0 observer hierarchy "test.observer_calls_task.bad"
    simulation.func private @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
    simulation.func private @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000002 : i64} {
      // expected-error @+1 {{task calls are not permitted in an observer entry}}
      simulation.task.call @callee(%ctx) arguments 1 to ^resume : !simulation.context
    ^resume:
      %false = arith.constant false
      simulation.return %false : i1
    }
  }
}

// -----

module {
  simulation.design @observer_bind_signature {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i16 design
    simulation.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_bind_signature.evaluator"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.observer_bind_signature.bad"
    simulation.func private @evaluator(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i8> {simulation.capture_kind = 2 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      simulation.return %false : i1
    }
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      // expected-error @+1 {{capture types must match evaluator arguments after context}}
      %bound = simulation.observer.bind @evaluator values(%ref, %ref : !simulation.ref<i16>, !simulation.ref<i16>) captures 1 : !simulation.observer<i1>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @observer_bind_dependency {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_bind_dependency.evaluator"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.observer_bind_dependency.bad"
    simulation.func private @evaluator(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      simulation.return %false : i1
    }
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{dependencies must be storage, argument-ref, net, named-event, or managed-watch handles}}
      %bound = simulation.observer.bind @evaluator values(%value : i8) captures 0 : !simulation.observer<i1>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @suspend_observe_primary_type {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.suspend_observe_primary_type.bad"
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      // expected-error @+1 {{primary operands must be observer handles}}
      simulation.suspend.observe %false, %false conditions 0 edges [0] indices [-1] to ^resume : i1, i1
    ^resume:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @suspend_observe_clauses {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.suspend_observe_clauses.bad"
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      // expected-error @+1 {{requires one initial value, edge, and condition index per primary}}
      simulation.suspend.observe %false conditions 0 edges [0, 1] indices [-1, -1] to ^resume : i1
    ^resume:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @suspend_observe_cancel_level_true_contract {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 9000001 in 0 observer hierarchy "test.suspend_observe_cancel_level_true_contract.evaluator"
    simulation.code_unit.decl 9000002 in 0 fork hierarchy "test.suspend_observe_cancel_level_true_contract.bad"
    simulation.func private @evaluator(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i1> {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      simulation.return %false : i1
    }
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          entry_kind = 13 : i32,
          code_unit_id = 9000002 : i64,
          home_region = 10 : i32,
          internal,
          schedule.concurrent_cancel,
          schedule.detached_controls
        } {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %bound = simulation.observer.bind @evaluator values(%ref, %ref : !simulation.ref<i1>, !simulation.ref<i1>) captures 1 : !simulation.observer<i1>
      %false = arith.constant false
      // expected-error @+1 {{concurrent cancel level-true suspension requires an internal detached priority concurrent-cancel fork in the reactive region}}
      simulation.suspend.observe %bound, %false conditions 0 edges [1] indices [-1] to ^resume {schedule.concurrent_cancel_level_true} : !simulation.observer<i1>, i1
    ^resume:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @suspend_observe_abort_level_true_contract {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 9000001 in 0 observer hierarchy "test.suspend_observe_abort_level_true_contract.evaluator"
    simulation.code_unit.decl 9000002 in 0 fork hierarchy "test.suspend_observe_abort_level_true_contract.bad"
    simulation.func private @evaluator(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i1> {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      simulation.return %false : i1
    }
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          entry_kind = 13 : i32,
          code_unit_id = 9000002 : i64,
          home_region = 10 : i32,
          internal,
          schedule.concurrent_abort,
          schedule.detached_controls
        } {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %bound = simulation.observer.bind @evaluator values(%ref, %ref : !simulation.ref<i1>, !simulation.ref<i1>) captures 1 : !simulation.observer<i1>
      %false = arith.constant false
      // expected-error @+1 {{concurrent abort level-true suspension requires an internal detached priority concurrent-abort fork in the reactive region}}
      simulation.suspend.observe %bound, %false conditions 0 edges [1] indices [-1] to ^resume {schedule.concurrent_abort_level_true} : !simulation.observer<i1>, i1
    ^resume:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @suspend_observe_cancel_level_true_empty {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000001 in 0 fork hierarchy "test.suspend_observe_cancel_level_true_empty.bad"
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          entry_kind = 13 : i32,
          code_unit_id = 9000001 : i64,
          home_region = 10 : i32,
          internal,
          schedule.concurrent_cancel,
          schedule.detached_controls,
          schedule.priority_signal_resume
        } {
      // expected-error @+1 {{requires at least one primary observer}}
      "simulation.suspend.observe"()[^resume] {condition_count = 0 : i32, condition_indices = array<i32>, edges = array<i32>, schedule.concurrent_cancel_level_true} : () -> ()
    ^resume:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @observer_bind_capture_abi {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000001 in 0 observer hierarchy "test.observer_bind_capture_abi.evaluator"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.observer_bind_capture_abi.bad"
    simulation.func private @evaluator(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %scalar: i8 {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      simulation.return %false : i1
    }
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %scalar = arith.constant 0 : i8
      // expected-error @+1 {{captures must use storage, net, driver, named-event, covergroup, or managed handles}}
      %bound = simulation.observer.bind @evaluator values(%scalar : i8) captures 1 : !simulation.observer<i1>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @suspend_observe_truncated_condition {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.code_unit.decl 9000001 in 0 observer hierarchy "test.suspend_observe_truncated_condition.primary"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.suspend_observe_truncated_condition.bad"
    simulation.func private @primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %false = arith.constant false
      simulation.return %false : i1
    }
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %dependency = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %primary = simulation.observer.bind @primary values(%dependency : !simulation.ref<!simulation.logic<1>>) captures 0 : !simulation.observer<i1>
      %false = arith.constant false
      // expected-error @+1 {{condition count exceeds the operand inventory}}
      simulation.suspend.observe %primary, %false conditions 1 edges [0] indices [0] to ^resume : !simulation.observer<i1>, i1
    ^resume:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @frozen_two_state_unknown {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "bad"
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          entry_kind = 8 : i32, code_unit_id = 1 : i64,
          simulation.bindings = [
            // expected-error @+2 {{two-state frozen constant must have a zero unknown plane}}
            // expected-error @+1 {{failed to parse SimConstantBindingAttr parameter 'value'}}
            #simulation.constant_binding<path = "P", value = #simulation.frozen_constant<value = [1 : i8, 1 : i8], isSigned = false> : i8>]
        } {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @binding_copy_in_role {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 task hierarchy "bad"
    // Only a formal has a direction to take copy-in from, so declining it
    // anywhere else names a rule that has no meaning there.
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32})
        attributes {
          entry_kind = 12 : i32, code_unit_id = 1 : i64,
          simulation.bindings = [
            // expected-error @below {{skipping copy-in is valid only for a formal-local argument binding}}
            #simulation.argument_binding<path = "value", argument = 1, kind = direct, copyOut = false, copyIn = false>]
        } {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @binding_role_type {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "bad"
    // expected-error @below {{lvalue-only binding requires a storage, net, or driver argument}}
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32})
        attributes {
          entry_kind = 8 : i32, code_unit_id = 1 : i64,
          simulation.bindings = [
            #simulation.argument_binding<path = "value", argument = 1, kind = lvalue_only, copyOut = false>]
        } {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @binding_local_type {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "bad"
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          entry_kind = 8 : i32, code_unit_id = 1 : i64,
          simulation.bindings = [
            // expected-error @+1 {{local binding type must be a normalized simulation value}}
            #simulation.local_binding<path = "local", type = !simulation.ref<i8>, automatic = false, patternVariable = false, isReturn = false>]
        } {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @binding_path_collision {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "bad"
    // expected-error @below {{both provide the source value for path 'same'}}
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32})
        attributes {
          entry_kind = 1 : i32, code_unit_id = 1 : i64,
          simulation.bindings = [
            #simulation.argument_binding<path = "same", argument = 1, kind = direct, copyOut = false>,
            #simulation.local_binding<path = "same", type = i32, automatic = false, patternVariable = false, isReturn = false>]
        } {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @binding_copy_out_pair {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 task hierarchy "bad"
    // expected-error @below {{copy-out destination path 'formal' requires a copy-out formal-local binding}}
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<i32> {simulation.capture_kind = 1 : i32})
        attributes {
          entry_kind = 12 : i32, code_unit_id = 1 : i64,
          simulation.bindings = [
            #simulation.argument_binding<path = "formal", argument = 1, kind = copy_out_destination, copyOut = false>]
        } {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @binding_multiple_returns {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "bad"
    // expected-error @below {{multiple local bindings are marked as the function return}}
    simulation.func private @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> i32
        attributes {
          entry_kind = 8 : i32, code_unit_id = 1 : i64,
          simulation.bindings = [
            #simulation.local_binding<path = "first", type = i32, automatic = false, patternVariable = false, isReturn = true>,
            #simulation.local_binding<path = "second", type = i32, automatic = false, patternVariable = false, isReturn = true>]
        } {
      %zero = arith.constant 0 : i32
      simulation.return %zero : i32
    }
  }
}

// -----

module {
  simulation.design @observed_delay {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "bad"
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          entry_kind = 1 : i32, code_unit_id = 1 : i64,
          home_region = 8 : i32
        } {
      %zero = simulation.time.constant 0
      // expected-error @+1 {{is not permitted in an observed-region code unit}}
      simulation.suspend.delay %zero to ^resume
    ^resume:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @postponed_store {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design hierarchy "top.value"
    simulation.code_unit.decl 1 in 0 task hierarchy "top.strobe"
    simulation.func @strobe(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<i8>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 12 : i32, code_unit_id = 1 : i64,
          home_region = 16 : i32
        } {
      %one = arith.constant 1 : i8
      // expected-error @+1 {{is not permitted in a read-only postponed code unit}}
      simulation.ref.store %one to %value : i8, !simulation.ref<i8>
      simulation.return
    }
  }
}

// -----

module {
  func.func @string_from_real(%value: f64) {
    // expected-error @+1 {{input must be a fixed packed value}}
    %string = simulation.string.from_packed %value :
      (f64) -> !simulation.string
    return
  }
}

// -----

module {
  func.func @bad_string_radix(%input: !simulation.string) {
    // expected-error @+1 {{expected '<'}}
    %value = simulation.string.parse_integer %input radix = 3
    return
  }
}

// -----

module {
  simulation.design @invalid_deferred_assertion_site {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "bad"
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @+1 {{deferred assertion site ID must be positive}}
      %first = simulation.assert.deferred_once 0
      simulation.return
    }
  }
}

// -----

module {
  func.func @managed_bits_replacement_too_wide(
      %reference: !simulation.managed_ref<i4, @C>,
      %replacement: i8, %low: i32) {
    // expected-error @+1 {{replacement width exceeds the packed field width}}
    simulation.managed.bits_dyn_store %replacement into %reference at %low :
      i8, !simulation.managed_ref<i4, @C>, i32
    return
  }
}
