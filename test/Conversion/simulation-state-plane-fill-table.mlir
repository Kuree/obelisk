// RUN: obelisk-opt %s --split-input-file --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// IEEE 1800-2023 6.8, Table 6-7.
// CHECK: llvm.mlir.global internal constant @__obelisk_state_initializers_v1(dense<[0, 268435456, 0, 1]> : tensor<4xi64>)
// CHECK: llvm.mlir.global internal @__obelisk_state_unknown()
// CHECK: llvm.mlir.zero : !llvm.array<33554440 x i8>
// CHECK: llvm.mlir.global internal @__obelisk_state_value()
// CHECK: llvm.mlir.zero : !llvm.array<33554440 x i8>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @large_initial_unknown {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<268435456> design
  }
}

// -----

// IEEE 1800-2023 6.6.5, 6.7.1, 23.3.3.7.
// Only the connected middle bits inherit the dominant pull value. The two
// outer bits of the wire retain Z; neither partial-byte fill covers padding.
// CHECK: llvm.mlir.global internal constant @__obelisk_state_initializers_v1(dense<[0, 1, 1, 1, 1, 2, 1, 0, 3, 1, 1, 1, 8, 2, 1, 0]> : tensor<16xi64>)
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @connected_initial_values {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.net.decl 1 in 0 : !simulation.logic<2> design {resolution_kind = 6 : i32}
    simulation.net.connect.decl 0 in 0 0[1] to 1[0] width 2 reversed = false rhs_dominates = true
  }
}
