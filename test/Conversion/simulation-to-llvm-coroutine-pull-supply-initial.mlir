// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// IEEE 1800-2017 6.6.5, 6.6.6, and 28.15 require these implicit drives to
// be visible at time zero, before any process executes. In declaration order,
// the initial value bits are 0, 1, 0, 1 and every unknown bit is clear.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @pull_supply_initial {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {resolution_kind = 5 : i32}
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {resolution_kind = 6 : i32}
    simulation.net.decl 2 in 0 : !simulation.logic<1> design {resolution_kind = 7 : i32}
    simulation.net.decl 3 in 0 : !simulation.logic<1> design {resolution_kind = 8 : i32}
    simulation.net.decl 4 in 0 : !simulation.logic<1> design {resolution_kind = 9 : i32}
  }
}

// IEEE 1800-2017 6.7.1 initializes trireg to x, represented by value bit 0
// and unknown bit 1 in the fifth declaration position.
// CHECK: llvm.mlir.global internal @__obelisk_state_unknown("\00\00\00\00\01\00\00\00\00\00\00\00\00")
// CHECK: llvm.mlir.global internal @__obelisk_state_value("\00\01\00\01\00\00\00\00\00\00\00\00\00")
