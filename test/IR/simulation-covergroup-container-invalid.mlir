// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
  func.func @truncated_covergroup(%size: i64) {
    // expected-error @+1 {{element metadata does not match the result container element type}}
    %array = simulation.container.create %size {
      type_id = 1 : i64, element_kind = #simulation.element_kind<bits>,
      element_flags = #simulation.element_flags<none>, value_size = 4 : i64,
      alignment = 4 : i64, bit_width = 32 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
    } : (i64) -> !simulation.dynamic_array<!simulation.covergroup_handle<@cg>>
    return
  }
}

// -----

module {
  func.func @four_state_covergroup(%size: i64) {
    // expected-error @+1 {{element metadata does not match the result container element type}}
    %queue = simulation.container.create %size {
      type_id = 1 : i64, element_kind = #simulation.element_kind<bits>,
      element_flags = #simulation.element_flags<four_state>, value_size = 8 : i64,
      alignment = 8 : i64, bit_width = 64 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      container_kind = #simulation.container_kind<queue>, bound = 0 : i64
    } : (i64) -> !simulation.queue<!simulation.covergroup_handle<@cg>, 0>
    return
  }
}

// -----

module {
  func.func @truncated_associative_covergroup() {
    // expected-error @+1 {{element metadata does not match the associative element type}}
    %array = simulation.assoc.create {
      type_id = 1 : i64, element_kind = #simulation.element_kind<bits>,
      element_flags = #simulation.element_flags<none>, value_size = 4 : i64,
      alignment = 4 : i64, bit_width = 32 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>,
      key_kind = #simulation.assoc_key_kind<signed>, key_width = 32 : i64
    } : () -> !simulation.assoc_array<i32, !simulation.covergroup_handle<@cg>, true, false>
    return
  }
}

// -----

module {
  func.func @managed_covergroup(%bound: i64) {
    // expected-error @+1 {{element metadata does not match the mailbox element type}}
    %mailbox = simulation.mailbox.create %bound {
      type_id = 1 : i64, element_kind = #simulation.element_kind<class_handle>,
      element_flags = #simulation.element_flags<none>, value_size = 8 : i64,
      alignment = 8 : i64, bit_width = 0 : i64,
      trace_offsets = array<i64>, trace_kinds = array<i32>
    } : (i64) -> !simulation.mailbox<!simulation.covergroup_handle<@cg>>
    return
  }
}
