// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

// IEEE 1800-2017 Table 23-1 makes a supply net dominate a collapsed uwire.
// The resulting component has supply resolution and therefore permits the
// multiple drivers that an effective uwire component would reject.
module {
  simulation.design @supply_dominates_uwire {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      resolution_kind = 2 : i32
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {
      resolution_kind = 7 : i32
    }
    simulation.driver.decl 0 in 0 drives 1 :
        !simulation.logic<1> design {resolution_kind = 7 : i32}
    simulation.driver.decl 1 in 0 drives 1 :
        !simulation.logic<1> design {resolution_kind = 7 : i32}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
  }
}

// CHECK: native-state bits=
// CHECK-DAG: driver 0 net=1
// CHECK-DAG: driver 1 net=1
