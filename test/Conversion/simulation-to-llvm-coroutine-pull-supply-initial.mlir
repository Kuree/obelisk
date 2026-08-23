// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// IEEE 1800-2017 6.6.5, 6.6.6, and 28.15 require these implicit drives to
// be visible at time zero, before any process executes. In declaration order,
// the initial value bits are 0, 1, 0, 1 and every unknown bit is clear.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @pull_supply_initial {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {resolution_kind = 5 : i32}
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design {resolution_kind = 6 : i32}
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design {resolution_kind = 7 : i32}
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design {resolution_kind = 8 : i32}
  }
}

// CHECK: llvm.mlir.global internal @__obelisk_state_unknown()
// CHECK: llvm.mlir.global internal @__obelisk_state_value("\0A\00\00\00\00\00\00\00\00")
