// RUN: not obelisk-opt %s --encode-obelisk-sim-to-bytecode 2>&1 | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @collapsed_delay {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, 11, 13>
    }
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
  }
}

// IEEE 1800-2017 23.3.3.7 selects the dominating port net's delay. The
// canonical connection currently has no internal/external direction, so this
// combination must not compile with per-declaration timing by accident.
// CHECK: error: net declaration delays on collapsed port nets require dominating-net delay selection
