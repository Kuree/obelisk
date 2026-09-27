// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @wrong_count {
    simulation.scope.decl 0
    // expected-error @+1 {{propagation delays must contain one uniform triple or one triple per net bit}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11>
    }
  }
}

// -----

module {
  simulation.design @negative {
    simulation.scope.decl 0
    // expected-error @+1 {{each net-delay triple must have nonnegative rise/fall delays and a nonnegative or -1 third delay}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, -1, 13>
    }
  }
}

// -----

module {
  simulation.design @trireg_negative_rise {
    simulation.scope.decl 0
    // expected-error @+1 {{each net-delay triple must have nonnegative rise/fall delays and a nonnegative or -1 third delay}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: -1, 11, 13>,
      resolution_kind = 9 : i32
    }
  }
}

// -----

module {
  simulation.design @trireg_invalid_decay {
    simulation.scope.decl 0
    // expected-error @+1 {{each net-delay triple must have nonnegative rise/fall delays and a nonnegative or -1 third delay}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, -2>,
      resolution_kind = 9 : i32
    }
  }
}
