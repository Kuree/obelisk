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
    // expected-error @+1 {{each net-delay triple must have nonnegative rise/fall delays and a nonnegative or -1 third delay}}
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, -1, 13>
    }
  }
}

// -----

module {
  obelisk_sim.design @trireg_negative_rise {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{each net-delay triple must have nonnegative rise/fall delays and a nonnegative or -1 third delay}}
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: -1, 11, 13>,
      resolution_kind = 9 : i32
    }
  }
}

// -----

module {
  obelisk_sim.design @trireg_invalid_decay {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{each net-delay triple must have nonnegative rise/fall delays and a nonnegative or -1 third delay}}
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, 11, -2>,
      resolution_kind = 9 : i32
    }
  }
}
