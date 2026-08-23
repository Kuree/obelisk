// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/ambiguous.mlir --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s --check-prefix=AMBIGUOUS
// RUN: not obelisk-opt %t/cycle.mlir --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s --check-prefix=CYCLE

//--- ambiguous.mlir
module {
  obelisk_sim.design @ambiguous {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 1, 1, 1>
    }
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 2, 2, 2>
    }
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    obelisk_sim.net.connect.decl 1 in 0 0[0] to 2[0] width 1 reversed = false rhs_dominates = true
  }
}

// AMBIGUOUS: error: delayed collapsed net has ambiguous dominating delays

//--- cycle.mlir
module {
  obelisk_sim.design @cycle {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 1, 1, 1>
    }
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    obelisk_sim.net.connect.decl 1 in 0 1[0] to 0[0] width 1 reversed = false rhs_dominates = true
    obelisk_sim.net.connect.decl 2 in 0 1[0] to 2[0] width 1 reversed = false rhs_dominates = true
  }
}

// CYCLE: error: delayed collapsed net has ambiguous port dominance
