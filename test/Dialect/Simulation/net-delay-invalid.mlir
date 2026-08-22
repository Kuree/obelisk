// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  obelisk_sim.design @wrong_count {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{propagation delays must contain one uniform triple or one triple per net bit}}
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, 11>
    }
  }
}

// -----

module {
  obelisk_sim.design @negative {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{each propagation-delay triple must be nonnegative or all -1}}
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, -1, 13>
    }
  }
}
