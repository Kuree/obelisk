// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/ambiguous.mlir --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s --check-prefix=AMBIGUOUS
// RUN: not obelisk-opt %t/cycle.mlir --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s --check-prefix=CYCLE

//--- ambiguous.mlir
module {
  simulation.design @ambiguous {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 1, 1, 1>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 2, 2, 2>
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 1 in 0 0[0] to 2[0] width 1 reversed = false rhs_dominates = true
  }
}

// AMBIGUOUS: error: delayed collapsed net has ambiguous dominating delays

//--- cycle.mlir
module {
  simulation.design @cycle {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 1, 1, 1>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 1 in 0 1[0] to 0[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 2 in 0 1[0] to 2[0] width 1 reversed = false rhs_dominates = true
  }
}

// CYCLE: error: delayed collapsed net has ambiguous port dominance
