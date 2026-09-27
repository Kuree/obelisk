// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode > /dev/null

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  simulation.design @vector_dominance {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "vector_dominance.empty"
    simulation.net.decl 0 in 0 : !simulation.logic<2> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, 13>,
      resolution_kind = 2 : i32
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    // Both drivers target the wire declaration, but only bit zero is in an
    // effective uwire component. The distinct bit-one driver is therefore
    // legal and exercises per-bit mixed-vector classification.
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<2> design {driven_low = 0 : i64, driven_width = 1 : i64}
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<2> design {driven_low = 1 : i64, driven_width = 1 : i64}
    simulation.func @empty(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
  }
}

// Bit zero inherits the external scalar delay while the isolated bit one
// remains immediate. The analyzed descriptor retains both behaviors.
// CHECK: net 0 {{.*}} delays=7,11,13;-
// CHECK: net 1 {{.*}} delays=7,11,13
