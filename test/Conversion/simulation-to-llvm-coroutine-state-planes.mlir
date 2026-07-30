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
  obelisk_sim.design @state_planes {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 1 :
        !obelisk_sim.logic<4> design
  }
}

// CHECK-LABEL: llvm.mlir.global internal @__obelisk_state_unknown()
// CHECK: %[[UZERO:.*]] = llvm.mlir.zero : !llvm.array<4 x i8>
// CHECK: %[[UFF1:.*]] = llvm.mlir.constant(-1 : i8) : i8
// CHECK: %[[BYTE1:.*]] = llvm.insertvalue %[[UFF1]], %[[UZERO]][1]
// CHECK: %[[UFF2:.*]] = llvm.mlir.constant(-1 : i8) : i8
// CHECK: %[[BYTE2:.*]] = llvm.insertvalue %[[UFF2]], %[[BYTE1]][2]
// CHECK: %[[U0F:.*]] = llvm.mlir.constant(15 : i8) : i8
// CHECK: %[[BYTE3:.*]] = llvm.insertvalue %[[U0F]], %[[BYTE2]][3]
// CHECK: llvm.return %[[BYTE3]]

// CHECK-LABEL: llvm.mlir.global internal @__obelisk_state_value()
// CHECK: %[[ZERO:.*]] = llvm.mlir.zero : !llvm.array<4 x i8>
// CHECK: %[[VFF:.*]] = llvm.mlir.constant(-1 : i8) : i8
// CHECK: %[[VBYTE2:.*]] = llvm.insertvalue %[[VFF]], %[[ZERO]][2]
// CHECK: %[[V0F:.*]] = llvm.mlir.constant(15 : i8) : i8
// CHECK: %[[VBYTE3:.*]] = llvm.insertvalue %[[V0F]], %[[VBYTE2]][3]
// CHECK: llvm.return %[[VBYTE3]]
