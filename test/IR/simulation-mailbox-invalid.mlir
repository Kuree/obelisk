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
  func.func @wrong_peek(%mailbox: !obelisk_sim.mailbox<i32>) {
    // expected-error @+1 {{message result must exactly match the mailbox element}}
    %ok, %value = "obelisk_sim.mailbox.try_peek"(%mailbox) :
      (!obelisk_sim.mailbox<i32>) -> (i1, i64)
    return
  }
}
