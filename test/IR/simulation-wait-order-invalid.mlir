// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  simulation.func @empty_order(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %event: !simulation.event {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{requires at least one event handle}}
    simulation.suspend.event_order %event events 0 to ^done :
        !simulation.event
  ^done(%event_arg: !simulation.event):
    simulation.return
  }
}

// -----

module {
  simulation.func @short_inventory(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %event: !simulation.event {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{event inventory exceeds the operand inventory}}
    simulation.suspend.event_order %event events 2 to ^done :
        !simulation.event
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func @wrong_handle(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %value: i32 {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{ordered values must be event handles}}
    simulation.suspend.event_order %value events 1 to ^done : i32
  ^done:
    simulation.return
  }
}
