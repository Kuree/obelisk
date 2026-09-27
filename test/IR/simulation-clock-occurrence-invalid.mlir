// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  simulation.func @zero_site(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{requires a positive 32-bit occurrence site}}
    simulation.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 0 to ^done :
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func private @empty_custom_mask(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  simulation.timing_check_coordinator} {
    // A Clause 31.5 custom spelling must select at least one transition class.
    // expected-error @+1 {{contains an invalid edge kind}}
    simulation.suspend.clock_set %clock conditions 0 edges [256]
        indices [-1] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func private @custom_mask_reserved_bit(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  simulation.timing_check_coordinator} {
    // expected-error @+1 {{contains an invalid edge kind}}
    simulation.suspend.clock_set %clock conditions 0 edges [320]
        indices [-1] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func private @predicate_count(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32},
      %condition: !simulation.ref<!simulation.logic<4>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  simulation.timing_check_coordinator} {
    // expected-error @+1 {{requires one predicate per clock condition}}
    simulation.suspend.clock_set %clock, %condition conditions 1 edges [1]
        indices [0] site 1 to ^done
        {condition_predicates = array<i32>} :
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<4>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func private @predicate_value(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32},
      %condition: !simulation.ref<!simulation.logic<4>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  simulation.timing_check_coordinator} {
    // expected-error @+1 {{contains an invalid clock condition predicate}}
    simulation.suspend.clock_set %clock, %condition conditions 1 edges [1]
        indices [0] site 1 to ^done
        {condition_predicates = array<i32: 10>} :
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<4>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func @permuted_conditions(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %a: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32},
      %b: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32},
      %ca: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32},
      %cb: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{condition indices must be in canonical ascending order}}
    simulation.suspend.clock_set %a, %b, %ca, %cb conditions 2 edges [1, 1]
        indices [1, 0] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func @bad_inventory(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{requires one edge and condition index per primary}}
    simulation.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1, -1] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func @bad_handle(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: i64 {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{clock primaries must be direct signal handles}}
    simulation.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 1 to ^done : i64
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func @unreferenced_condition(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32},
      %condition: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{contains an unreferenced condition handle}}
    simulation.suspend.clock_set %clock, %condition conditions 1 edges [1]
        indices [-1] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>,
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func @bad_condition_type(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32},
      %condition: i1 {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    // expected-error @+1 {{clock conditions must be direct signal handles or one-bit observer tokens}}
    simulation.suspend.clock_set %clock, %condition conditions 1 edges [1]
        indices [0] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>, i1
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func @unbound_observer_condition(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    %false = arith.constant false
    %condition = builtin.unrealized_conversion_cast %false :
        i1 to !simulation.observer<i1>
    // expected-error @+1 {{clock condition observer must be produced by observer.bind}}
    simulation.suspend.clock_set %clock, %condition conditions 1 edges [1]
        indices [0] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>, !simulation.observer<i1>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func @wide_observer_condition(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 1 : i32} {
    %zero = arith.constant 0 : i2
    %condition = builtin.unrealized_conversion_cast %zero :
        i2 to !simulation.observer<i2>
    // expected-error @+1 {{clock conditions must be direct signal handles or one-bit observer tokens}}
    simulation.suspend.clock_set %clock, %condition conditions 1 edges [1]
        indices [0] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>, !simulation.observer<i2>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func @public_marked_coordinator(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  simulation.multiclock_sequence_coordinator} {
    // expected-error @+1 {{requires a private design-domain assertion or timing-check coordinator}}
    simulation.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func private @assertion_slot_final(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 8 : i32,
                  simulation.multiclock_sequence_coordinator} {
    // Slot-final admission is a Clause 31 numeric-time lookahead and must not
    // alter the ordinary exact assertion-clock scheduler path.
    // expected-error @+1 {{slot_final is reserved for a timing-check coordinator}}
    simulation.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 1 to ^done {slot_final} :
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}

// -----

module {
  simulation.func private @timing_check_in_active(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 1 : i32})
      attributes {entry_kind = 3 : i32, domain = 0 : i32,
                  home_region = 2 : i32,
                  simulation.timing_check_coordinator} {
    // IEEE 1800-2017 Clause 31 simultaneous checks require the finalized
    // design time slot, so a timing coordinator may not run in Active.
    // expected-error @+1 {{requires a private design-domain assertion or timing-check coordinator}}
    simulation.suspend.clock_set %clock conditions 0 edges [1]
        indices [-1] site 1 to ^done :
        !simulation.ref<!simulation.logic<1>>
  ^done:
    simulation.return
  }
}
