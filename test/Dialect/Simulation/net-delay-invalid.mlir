// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  obelisk_sim.design @wrong_count {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{propagation delays must contain rise, fall, and turn-off values}}
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, 11>
    }
  }
}

// -----

module {
  obelisk_sim.design @negative {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{propagation delays must be nonnegative}}
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, -1, 13>
    }
  }
}
