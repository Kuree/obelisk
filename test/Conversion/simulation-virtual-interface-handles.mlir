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
  simulation.design @virtual_interface_handles {
    simulation.scope.decl 0 hierarchy "top"
    simulation.scope.decl 1 parent 0 hierarchy "top.first" interface "@bus"
    simulation.scope.decl 2 parent 0 hierarchy "top.second" interface "@bus"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    simulation.storage.decl 0 in 0
      : !simulation.virtual_interface<"@bus", ""> design
        hierarchy "top.vif"

    simulation.func @exercise(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %null = simulation.virtual_interface.null
        : !simulation.virtual_interface<"@bus", "">
      %first = simulation.virtual_interface.bind 1
        : !simulation.virtual_interface<"@bus", "">
      %second = simulation.virtual_interface.bind 2
        : !simulation.virtual_interface<"@bus", "">
      %restricted = simulation.virtual_interface.cast %first
        : !simulation.virtual_interface<"@bus", ""> to
          !simulation.virtual_interface<"@bus", "driver">
      %null_equal = simulation.virtual_interface.equal %null, %null
        : !simulation.virtual_interface<"@bus", "">,
          !simulation.virtual_interface<"@bus", "">
      %instance_equal = simulation.virtual_interface.equal %first, %second
        : !simulation.virtual_interface<"@bus", "">,
          !simulation.virtual_interface<"@bus", "">
      %view_equal = simulation.virtual_interface.equal %restricted, %first
        : !simulation.virtual_interface<"@bus", "driver">,
          !simulation.virtual_interface<"@bus", "">
      simulation.return
    }
  }
}

// NATIVE-LABEL: llvm.func @exercise
// NATIVE-DAG: %[[NULL:.*]] = llvm.mlir.constant(0 : i64)
// NATIVE-DAG: %[[FIRST:.*]] = llvm.mlir.constant(1 : i64)
// NATIVE-DAG: %[[SECOND:.*]] = llvm.mlir.constant(2 : i64)
// NATIVE-DAG: llvm.mlir.constant(true) : i1
// NATIVE-DAG: llvm.mlir.constant(false) : i1
// NATIVE-NOT: simulation.virtual_interface

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
