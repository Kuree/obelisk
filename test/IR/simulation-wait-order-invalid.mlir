// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  obelisk_sim.func @empty_order(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %event: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{requires at least one event handle}}
    obelisk_sim.suspend.event_order %event events 0 to ^done :
        !obelisk_sim.event
  ^done(%event_arg: !obelisk_sim.event):
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func @short_inventory(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %event: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{event inventory exceeds the operand inventory}}
    obelisk_sim.suspend.event_order %event events 2 to ^done :
        !obelisk_sim.event
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func @wrong_handle(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %value: i32 {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{ordered values must be event handles}}
    obelisk_sim.suspend.event_order %value events 1 to ^done : i32
  ^done:
    obelisk_sim.return
  }
}
