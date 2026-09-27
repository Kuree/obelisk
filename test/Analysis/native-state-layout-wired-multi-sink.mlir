// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  simulation.design @wired_multi_sink {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {
      resolution_kind = 3 : i32
    }
    simulation.net.decl 2 in 0 : !simulation.logic<1> design {
      resolution_kind = 3 : i32
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 1 in 0 0[0] to 2[0] width 1 reversed = false rhs_dominates = true
  }
}

// IEEE 1800-2017 Table 23-1 gives both sink endpoints the same effective
// wand kind, so a unique endpoint is unnecessary.
// CHECK-DAG: net 0
// CHECK-DAG: net 1
// CHECK-DAG: net 2
