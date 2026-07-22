// RUN: not obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' 2>&1 | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @unsupported {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.unsupported.min.9000001"
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.func @min(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %lhs: i8 {obelisk_sim.capture_kind = 1 : i32},
        %rhs: i8 {obelisk_sim.capture_kind = 1 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      // arith.minui is valid arith IR, but is deliberately outside the closed
      // normalized bytecode boundary until it has an interpreter opcode.
      %result = arith.minui %lhs, %rhs : i8
      obelisk_sim.return %result : i8
    }
  }
}

// CHECK: error: 'arith.minui' op has no design-bytecode semantics
