// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

module {
  obelisk_sim.design @net_dominance {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 1, 2, 3>
    }
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, 11, 13>
    }
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    obelisk_sim.net.connect.decl 1 in 0 1[0] to 2[0] width 1 reversed = false rhs_dominates = true
  }
}

// The transitive external endpoint dominates the complete collapsed net.
// CHECK: net 0 {{.*}} delays=7,11,13
// CHECK: net 1 {{.*}} delays=7,11,13
// CHECK: net 2 {{.*}} delays=7,11,13
