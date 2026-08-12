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
  obelisk_sim.design @chandle_handles {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.chandle design
      hierarchy "top.handle"

    obelisk_sim.func @exercise(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %first = obelisk_sim.chandle.null : !obelisk_sim.chandle
      %second = obelisk_sim.chandle.null : !obelisk_sim.chandle
      %equal = obelisk_sim.chandle.equal %first, %second
        : !obelisk_sim.chandle
      obelisk_sim.return
    }
  }
}

// NATIVE-LABEL: llvm.func @exercise
// NATIVE-DAG: %[[FIRST:.*]] = llvm.mlir.constant(0 : i64)
// NATIVE-DAG: %[[SECOND:.*]] = llvm.mlir.constant(0 : i64)
// NATIVE: llvm.mlir.constant(true) : i1
// NATIVE-NOT: obelisk_sim.chandle

// BYTECODE: obelisk.bytecode.image = array<i8:
// BYTECODE: obelisk.execution.state_bits = 64 : i64

// INSTRUCTIONS: constants: 00000000000000000000000000000000
// INSTRUCTIONS-NEXT: 0: opcode=1 flags=0 dst=1 {{.*}} imm=0
// INSTRUCTIONS-NEXT: 1: opcode=1 flags=0 dst=2 {{.*}} imm=8
// INSTRUCTIONS-NEXT: 2: opcode=17 flags=0 dst=3 src0=1 src1=2
