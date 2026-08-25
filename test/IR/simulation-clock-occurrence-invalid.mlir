// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  obelisk_sim.func @zero_site(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{requires a positive 32-bit occurrence site}}
    obelisk_sim.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 0 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func @permuted_conditions(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %a: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32},
      %b: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32},
      %ca: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32},
      %cb: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{condition indices must be in canonical ascending order}}
    obelisk_sim.suspend.clock_set %a, %b, %ca, %cb conditions 2 edges [1, 1]
        indices [1, 0] site 1 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func @bad_inventory(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{requires one edge and condition index per primary}}
    obelisk_sim.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1, -1] site 1 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func @bad_handle(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: i64 {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{clock primaries must be direct signal handles}}
    obelisk_sim.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 1 to ^done : i64
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func @unreferenced_condition(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32},
      %condition: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{contains an unreferenced condition handle}}
    obelisk_sim.suspend.clock_set %clock, %condition conditions 1 edges [1]
        indices [-1] site 1 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func @public_marked_coordinator(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  obelisk_sim.multiclock_sequence_coordinator} {
    // expected-error @+1 {{requires a private Observed design-domain multi-clock coordinator}}
    obelisk_sim.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 1 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}
