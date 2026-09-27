// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode > /dev/null

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  simulation.design @trireg_delay_layout {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "trireg_delay_layout.empty"
    // Port-collapse normalization gives the declared wire alias the same
    // effective trireg delay triple.
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, -1>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, -1>,
      resolution_kind = 9 : i32
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    simulation.func @empty(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
  }
}

// IEEE 1800-2017 28.16.2: omitted charge decay becomes the runtime's
// infinite-decay sentinel while rise and fall remain ordinary finite delays.
// CHECK: net 0 {{.*}} delays=7,11,18446744073709551615
// CHECK: net 1 {{.*}} delays=7,11,18446744073709551615
