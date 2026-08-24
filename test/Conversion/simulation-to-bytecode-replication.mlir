// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s

// IEEE 1800-2017 11.4.12.1: a wide packed replication is one bytecode
// instruction. Its instruction and register counts do not scale with the
// replication multiplier.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @replication {
    obelisk_sim.code_unit.decl 9100001 in 0 function hierarchy "test.replication.9100001"
    obelisk_sim.scope.decl 0 hierarchy "top"

    obelisk_sim.func @replicate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<65536>
        attributes {entry_kind = 8 : i32, code_unit_id = 9100001 : i64} {
      %result = obelisk_sim.logic.replicate %value times 8192 :
          !obelisk_sim.logic<8> -> !obelisk_sim.logic<65536>
      obelisk_sim.return %result : !obelisk_sim.logic<65536>
    }
  }
}

// Opcode 61 is the append-only packed-replication instruction.
// CHECK-COUNT-1: opcode=61 flags=0
// CHECK-SAME: imm=8192
// CHECK-NOT: opcode=20
