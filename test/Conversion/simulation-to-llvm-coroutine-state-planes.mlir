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

// Each plane is one byte blob rather than a chain of inserts: an insertvalue
// per set byte constant-folds into a fresh whole-plane array, which is
// quadratic once a design carries a large unpacked array.
//
// Byte 0 holds the two-state i8 root, so it starts known-zero in both planes.
// Byte 1 holds the four-state logic<8> root: unknown, value zero. Byte 2 holds
// the two logic<4> nets and byte 3 the driver, all at high impedance.
// Generated scalar accesses may use an unaligned word at the final root, so
// the canonical 32-bit plane carries one private 8-byte guard word.
// CHECK: llvm.mlir.global internal @__obelisk_state_unknown("\00\FF\FF\0F\00\00\00\00\00\00\00\00")
// CHECK: llvm.mlir.global internal @__obelisk_state_value("\00\00\FF\0F\00\00\00\00\00\00\00\00")
