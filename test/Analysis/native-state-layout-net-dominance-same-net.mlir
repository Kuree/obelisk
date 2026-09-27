// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode > /dev/null

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  simulation.design @same_net_dominance {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "same_net_dominance.empty"
    simulation.net.decl 0 in 0 : !simulation.logic<2> design {
      propagation_delays = array<i64: 1, 2, 3, 7, 11, 13>
    }
    simulation.net.connect.decl 0 in 0 0[0] to 0[1] width 1 reversed = false rhs_dominates = true
    simulation.func @empty(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
  }
}

// The right endpoint dominates even though both endpoint bits belong to the
// same declared vector net.
// CHECK: net 0 {{.*}} delays=7,11,13;7,11,13
