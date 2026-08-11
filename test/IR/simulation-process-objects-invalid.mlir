// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  func.func @outside_sim_func(%process: !obelisk_sim.process) {
    // expected-error @below {{'obelisk_sim.process.control' op must be nested in obelisk_sim.func}}
    obelisk_sim.process.control kill %process to ^continued
  ^continued:
    func.return
  }
}

// -----

module {
  // expected-error @below {{'obelisk_sim.process.current' op must be nested in obelisk_sim.func}}
  %current = obelisk_sim.process.current
}

// -----

module {
  %null = obelisk_sim.process.null
  // expected-error @below {{'obelisk_sim.process.status' op must be nested in obelisk_sim.func}}
  %status = obelisk_sim.process.status %null
}

// -----

module {
  obelisk_sim.design @observer_control {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "top.observer"
    obelisk_sim.func @observer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64} {
      %current = obelisk_sim.process.current
      // expected-error @below {{'obelisk_sim.process.control' op is not permitted in an observer entry}}
      obelisk_sim.process.control suspend %current to ^continued
    ^continued:
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }
  }
}

// -----

module {
  obelisk_sim.design @postponed_control {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 task hierarchy "top.postponed"
    obelisk_sim.func @postponed(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, home_region = 16 : i32,
                    code_unit_id = 1 : i64} {
      %current = obelisk_sim.process.current
      // expected-error @below {{'obelisk_sim.process.control' op is not permitted in a read-only postponed code unit}}
      obelisk_sim.process.control resume %current to ^continued
    ^continued:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_process_continuation {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %current = obelisk_sim.process.current
      %value = arith.constant 1 : i32
      // expected-error @below {{type mismatch for bb argument #0 of successor #0}}
      obelisk_sim.process.control suspend %current to ^continued(%value : i32)
    ^continued(%forwarded: i64):
      obelisk_sim.return
    }
  }
}
