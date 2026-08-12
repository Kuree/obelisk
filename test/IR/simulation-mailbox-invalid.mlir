// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  func.func @wrong_put(%mailbox: !obelisk_sim.mailbox<i32>, %value: i64) {
    // expected-error @+1 {{message type must exactly match the mailbox element}}
    %ok = "obelisk_sim.mailbox.try_put"(%mailbox, %value) :
      (!obelisk_sim.mailbox<i32>, i64) -> i1
    return
  }
}

// -----

module {
  func.func @wrong_element_metadata(%bound: i64) {
    // expected-error @+1 {{element metadata does not match the mailbox element type}}
    %mailbox = "obelisk_sim.mailbox.create"(%bound) {
      alignment = 8 : i64,
      bit_width = 64 : i64,
      element_flags = 0 : i32,
      element_kind = 1 : i32,
      trace_kinds = array<i32>,
      trace_offsets = array<i64>,
      type_id = 1 : i64,
      value_size = 8 : i64
    } : (i64) -> !obelisk_sim.mailbox<!obelisk_sim.string>
    return
  }
}

// -----

module {
  func.func @wrong_peek(%mailbox: !obelisk_sim.mailbox<i32>) {
    // expected-error @+1 {{message result must exactly match the mailbox element}}
    %ok, %value = "obelisk_sim.mailbox.try_peek"(%mailbox) :
      (!obelisk_sim.mailbox<i32>) -> (i1, i64)
    return
  }
}
