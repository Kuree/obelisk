// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @bitcasts {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.bitcasts.f64_to_i64.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.bitcasts.i64_to_f64.9000002"
    simulation.code_unit.decl 9000003 in 0 function hierarchy "test.bitcasts.f32_to_i32.9000003"
    simulation.code_unit.decl 9000004 in 0 function hierarchy "test.bitcasts.i32_to_f32.9000004"
    simulation.scope.decl 0 hierarchy "top"

    simulation.func @f64_to_i64(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: f64 {simulation.capture_kind = 1 : i32}) -> i64
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %result = arith.bitcast %value : f64 to i64
      simulation.return %result : i64
    }

    simulation.func @i64_to_f64(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i64 {simulation.capture_kind = 1 : i32}) -> f64
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = arith.bitcast %value : i64 to f64
      simulation.return %result : f64
    }

    simulation.func @f32_to_i32(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: f32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %result = arith.bitcast %value : f32 to i32
      simulation.return %result : i32
    }

    simulation.func @i32_to_f32(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> f32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %result = arith.bitcast %value : i32 to f32
      simulation.return %result : f32
    }
  }
}

// Opcode 59 is the append-only scalar integer/float bitcast instruction.
// Each function contains one bitcast, so all four direction/width variants
// must survive instruction selection.
// CHECK-COUNT-4: opcode=59 flags=0
