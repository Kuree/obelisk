// RUN: not obelisk-opt %s --encode-obelisk-sim-to-bytecode 2>&1 | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @collapsed_delay {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      propagation_delays = array<i64: 7, 11, 13>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
  }
}

// IEEE 1800-2017 23.3.3.7 selects the dominating port net's delay. This
// hand-written connection omits its internal/external direction, so it must
// not compile with per-declaration timing by accident.
// CHECK: error: delayed collapsed net is missing port-dominance direction
