// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=INSTRUCTIONS

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @virtual_interface_handles {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.first"
    obelisk_sim.scope.decl 2 parent 0 hierarchy "top.second"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    obelisk_sim.storage.decl 0 in 0
      : !obelisk_sim.virtual_interface<"@bus", ""> design
        hierarchy "top.vif"

    obelisk_sim.func @exercise(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %null = obelisk_sim.virtual_interface.null
        : !obelisk_sim.virtual_interface<"@bus", "">
      %first = obelisk_sim.virtual_interface.bind 1
        : !obelisk_sim.virtual_interface<"@bus", "">
      %second = obelisk_sim.virtual_interface.bind 2
        : !obelisk_sim.virtual_interface<"@bus", "">
      %restricted = obelisk_sim.virtual_interface.cast %first
        : !obelisk_sim.virtual_interface<"@bus", ""> to
          !obelisk_sim.virtual_interface<"@bus", "driver">
      %null_equal = obelisk_sim.virtual_interface.equal %null, %null
        : !obelisk_sim.virtual_interface<"@bus", "">,
          !obelisk_sim.virtual_interface<"@bus", "">
      %instance_equal = obelisk_sim.virtual_interface.equal %first, %second
        : !obelisk_sim.virtual_interface<"@bus", "">,
          !obelisk_sim.virtual_interface<"@bus", "">
      %view_equal = obelisk_sim.virtual_interface.equal %restricted, %first
        : !obelisk_sim.virtual_interface<"@bus", "driver">,
          !obelisk_sim.virtual_interface<"@bus", "">
      obelisk_sim.return
    }
  }
}

// NATIVE-LABEL: llvm.func @exercise
// NATIVE-DAG: %[[NULL:.*]] = llvm.mlir.constant(0 : i64)
// NATIVE-DAG: %[[FIRST:.*]] = llvm.mlir.constant(1 : i64)
// NATIVE-DAG: %[[SECOND:.*]] = llvm.mlir.constant(2 : i64)
// NATIVE-DAG: llvm.mlir.constant(true) : i1
// NATIVE-DAG: llvm.mlir.constant(false) : i1
// NATIVE-NOT: obelisk_sim.virtual_interface

// BYTECODE-DAG: obelisk.bytecode.image = array<i8:
// BYTECODE-DAG: obelisk.execution.state_bits = 64 : i64

// Null, two scope identities, a zero-cost view cast, and three comparisons.
// INSTRUCTIONS: constants: 00000000000000000100000000000000020000000000000000
// INSTRUCTIONS-NEXT: 0: opcode=1 flags=0 dst=1 {{.*}} imm=0
// INSTRUCTIONS-NEXT: 1: opcode=1 flags=0 dst=2 {{.*}} imm=8
// INSTRUCTIONS-NEXT: 2: opcode=1 flags=0 dst=3 {{.*}} imm=16
// INSTRUCTIONS-NEXT: 3: opcode=2 flags=0 dst=4 src0=2
// INSTRUCTIONS-NEXT: 4: opcode=17 flags=0 dst=5 src0=1 src1=1
// INSTRUCTIONS-NEXT: 5: opcode=17 flags=0 dst=6 src0=2 src1=3
// INSTRUCTIONS-NEXT: 6: opcode=17 flags=0 dst=7 src0=4 src1=2
