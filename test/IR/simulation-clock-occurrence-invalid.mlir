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
  obelisk_sim.func private @empty_custom_mask(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  obelisk_sim.timing_check_coordinator} {
    // A Clause 31.5 custom spelling must select at least one transition class.
    // expected-error @+1 {{contains an invalid edge kind}}
    obelisk_sim.suspend.clock_set %clock conditions 0 edges [256]
        indices [-1] site 1 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func private @custom_mask_reserved_bit(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  obelisk_sim.timing_check_coordinator} {
    // expected-error @+1 {{contains an invalid edge kind}}
    obelisk_sim.suspend.clock_set %clock conditions 0 edges [320]
        indices [-1] site 1 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func private @predicate_count(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32},
      %condition: !obelisk_sim.ref<!obelisk_sim.logic<4>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  obelisk_sim.timing_check_coordinator} {
    // expected-error @+1 {{requires one predicate per clock condition}}
    obelisk_sim.suspend.clock_set %clock, %condition conditions 1 edges [1]
        indices [0] site 1 to ^done
        {condition_predicates = array<i32>} :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<4>>
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func private @predicate_value(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32},
      %condition: !obelisk_sim.ref<!obelisk_sim.logic<4>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  obelisk_sim.timing_check_coordinator} {
    // expected-error @+1 {{contains an invalid clock condition predicate}}
    obelisk_sim.suspend.clock_set %clock, %condition conditions 1 edges [1]
        indices [0] site 1 to ^done
        {condition_predicates = array<i32: 10>} :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<4>>
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
    // expected-error @+1 {{requires a private design-domain assertion or timing-check coordinator}}
    obelisk_sim.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 1 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func private @assertion_slot_final(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  obelisk_sim.multiclock_sequence_coordinator} {
    // Slot-final admission is a Clause 31 numeric-time lookahead and must not
    // alter the ordinary exact assertion-clock scheduler path.
    // expected-error @+1 {{slot_final is reserved for a timing-check coordinator}}
    obelisk_sim.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 1 to ^done {slot_final} :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}

// -----

module {
  obelisk_sim.func private @timing_check_in_active(
      %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
      %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 2 : i32,
                  obelisk_sim.timing_check_coordinator} {
    // IEEE 1800-2017 Clause 31 simultaneous checks require the finalized
    // design time slot, so a timing coordinator may not run in Active.
    // expected-error @+1 {{requires a private design-domain assertion or timing-check coordinator}}
    obelisk_sim.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 1 to ^done :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
  ^done:
    obelisk_sim.return
  }
}
