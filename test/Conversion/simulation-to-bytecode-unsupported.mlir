// RUN: not obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' 2>&1 | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @unsupported {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.unsupported.min.9000001"
    simulation.scope.decl 0 hierarchy "top"
    simulation.func @min(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %lhs: i8 {simulation.capture_kind = 1 : i32},
        %rhs: i8 {simulation.capture_kind = 1 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      // arith.minui is valid arith IR, but is deliberately outside the closed
      // normalized bytecode boundary until it has an interpreter opcode.
      %result = arith.minui %lhs, %rhs : i8
      simulation.return %result : i8
    }
  }
}

// CHECK: error: 'arith.minui' op has no design-bytecode semantics
