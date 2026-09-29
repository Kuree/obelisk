// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// Verify typed state-plane initialization directly at the lowering boundary.
// Two-state storage starts known-zero. Four-state storage starts unknown.
// Undriven nets, driven nets, and their drivers independently start at high
// impedance.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @state_planes {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.net.decl 1 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 1 :
        !simulation.logic<4> design
  }
}

// IEEE 1800-2023 6.7.1, 6.8: initialization is represented by sorted bit
// ranges. The two-state byte and the padding between four-bit roots stay zero.
// CHECK: llvm.mlir.global internal constant @__obelisk_state_initializers_v1(dense<[8, 8, 0, 1, 16, 4, 1, 1, 24, 4, 1, 1, 32, 4, 1, 1]> : tensor<16xi64>)
// CHECK: llvm.mlir.global internal @__obelisk_state_unknown()
// CHECK: llvm.mlir.zero : !llvm.array<13 x i8>
// CHECK: llvm.mlir.global internal @__obelisk_state_value()
// CHECK: llvm.mlir.zero : !llvm.array<13 x i8>
