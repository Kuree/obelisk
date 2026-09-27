// RUN: obelisk-opt %s -verify-diagnostics

module {
  simulation.func @too_many_clocks(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{requires between one and 64 clock primaries}}
    simulation.suspend.clock_set %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock conditions 0
        edges [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
        indices [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}
