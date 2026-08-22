// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode > /dev/null

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  obelisk_sim.design @vector_dominance {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "vector_dominance.empty"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, 11, 13>
    }
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    obelisk_sim.func @empty(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return
    }
  }
}

// Bit zero inherits the external scalar delay while the isolated bit one
// remains immediate. The analyzed descriptor retains both behaviors.
// CHECK: net 0 {{.*}} delays=7,11,13;-
// CHECK: net 1 {{.*}} delays=7,11,13
