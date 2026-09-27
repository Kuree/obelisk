// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @valid_collapsed_trireg_decay {
    simulation.scope.decl 0
    // Port-delay normalization copies the effective trireg triple onto this
    // declared wire alias. IEEE 1800-2017 23.3.3.7 and 28.16.2 make the
    // omitted third value legal because the trireg endpoint dominates bit 0.
    simulation.net.decl 0 in 0 : !simulation.logic<2> design {
      propagation_delays = array<i64: 7, 11, -1, 3, 5, 7>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, -1>,
      resolution_kind = 9 : i32
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
  }
}

// -----

module {
  simulation.design @isolated_wire_omitted_third {
    simulation.scope.decl 0
    // expected-error @+1 {{omitted charge decay requires an effective trireg component}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, -1>
    }
  }
}

// -----

module {
  simulation.design @partial_vector_effective_kind {
    simulation.scope.decl 0
    // Bit 0 inherits trireg resolution, but the unconnected bit 1 remains an
    // ordinary wire and cannot use the omitted-decay sentinel.
    // expected-error @+1 {{omitted charge decay requires an effective trireg component}}
    simulation.net.decl 0 in 0 : !simulation.logic<2> design {
      propagation_delays = array<i64: 7, 11, -1>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, -1>,
      resolution_kind = 9 : i32
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
  }
}

// -----

module {
  simulation.design @wired_net_dominates_trireg {
    simulation.scope.decl 0
    // expected-error @+1 {{omitted charge decay requires an effective trireg component}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, -1>,
      resolution_kind = 3 : i32
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, -1>,
      resolution_kind = 9 : i32
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = false
  }
}
