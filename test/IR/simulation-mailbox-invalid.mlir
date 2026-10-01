// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  func.func @wrong_put(%mailbox: !simulation.mailbox<i32>, %value: i64) {
    // expected-error @+1 {{message type must exactly match the mailbox element}}
    %ok = simulation.mailbox.try_put %mailbox, %value :
      (!simulation.mailbox<i32>, i64) -> i1
    return
  }
}

// -----

module {
  simulation.func @missing_mailbox_root(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %mailbox: !simulation.mailbox<i32> {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{requires the mailbox as a continuation operand to preserve its GC root}}
    simulation.suspend.mailbox %mailbox not_empty to ^done :
      !simulation.mailbox<i32>
  ^done:
    simulation.return
  }
}

// -----

module {
  func.func @wrong_element_metadata(%bound: i64) {
    // expected-error @+1 {{element metadata does not match the mailbox element type}}
    %mailbox = simulation.mailbox.create %bound {
      alignment = 8 : i64,
      bit_width = 64 : i64,
      element_flags = #simulation.element_flags<none>,
      element_kind = #simulation.element_kind<bits>,
      trace_kinds = array<i32>,
      trace_offsets = array<i64>,
      type_id = 1 : i64,
      value_size = 8 : i64
    } : (i64) -> !simulation.mailbox<!simulation.string>
    return
  }
}

// -----

module {
  func.func @wrong_peek(%mailbox: !simulation.mailbox<i32>) {
    // expected-error @+1 {{message result must exactly match the mailbox element}}
    %ok, %value = simulation.mailbox.try_peek %mailbox :
      (!simulation.mailbox<i32>) -> (i1, i64)
    return
  }
}
