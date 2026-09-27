// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

module {
  simulation.design @net_dominance {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 1, 2, 3>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, 13>
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 1 in 0 1[0] to 2[0] width 1 reversed = false rhs_dominates = true
  }
}

// The transitive external endpoint dominates the complete collapsed net.
// CHECK: net 0 {{.*}} delays=7,11,13
// CHECK: net 1 {{.*}} delays=7,11,13
// CHECK: net 2 {{.*}} delays=7,11,13
