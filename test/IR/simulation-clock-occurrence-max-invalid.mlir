// RUN: obelisk-opt %s -verify-diagnostics

module {
  obelisk_sim.func @too_many_clocks(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{requires between one and 64 clock primaries}}
    obelisk_sim.suspend.clock_set %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock conditions 0
        edges [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
        indices [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1] site 1 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}
