// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  func.func @outside_sim_func(%process: !simulation.process) {
    // expected-error @below {{'simulation.process.control' op must be nested in simulation.func}}
    simulation.process.control kill %process to ^continued
  ^continued:
    func.return
  }
}

// -----

module {
  // expected-error @below {{'simulation.process.current' op must be nested in simulation.func}}
  %current = simulation.process.current
}

// -----

module {
  %null = simulation.process.null
  // expected-error @below {{'simulation.process.status' op must be nested in simulation.func}}
  %status = simulation.process.status %null
}

// -----

module {
  %null = simulation.process.null
  // expected-error @below {{'simulation.process.random_state' op must be nested in simulation.func}}
  %state, %increment = simulation.process.random_state %null
}

// -----

module {
  simulation.design @observer_control {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 observer hierarchy "top.observer"
    simulation.func @observer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64} {
      %current = simulation.process.current
      // expected-error @below {{'simulation.process.control' op is not permitted in an observer entry}}
      simulation.process.control suspend %current to ^continued
    ^continued:
      %false = arith.constant false
      simulation.return %false : i1
    }
  }
}

// -----

module {
  simulation.design @postponed_control {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 task hierarchy "top.postponed"
    simulation.func @postponed(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, home_region = 16 : i32,
                    code_unit_id = 1 : i64} {
      %current = simulation.process.current
      // expected-error @below {{'simulation.process.control' op is not permitted in a read-only postponed code unit}}
      simulation.process.control resume %current to ^continued
    ^continued:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_process_continuation {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.bad"
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %current = simulation.process.current
      %value = arith.constant 1 : i32
      // expected-error @below {{type mismatch for bb argument #0 of successor #0}}
      simulation.process.control suspend %current to ^continued(%value : i32)
    ^continued(%forwarded: i64):
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @postponed_process_random_write {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 task hierarchy "top.postponed_rng"
    simulation.func @postponed(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, home_region = 16 : i32,
                    code_unit_id = 1 : i64} {
      %current = simulation.process.current
      %seed = arith.constant 1 : i64
      // expected-error @below {{'simulation.process.set_random_state' op is not permitted in a read-only postponed code unit}}
      simulation.process.set_random_state %current, %seed, %seed
      simulation.return
    }
  }
}
