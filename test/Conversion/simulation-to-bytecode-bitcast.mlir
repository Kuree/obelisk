// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @bitcasts {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.bitcasts.f64_to_i64.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.bitcasts.i64_to_f64.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 function hierarchy "test.bitcasts.f32_to_i32.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 function hierarchy "test.bitcasts.i32_to_f32.9000004"
    obelisk_sim.scope.decl 0 hierarchy "top"

    obelisk_sim.func @f64_to_i64(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: f64 {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %result = arith.bitcast %value : f64 to i64
      obelisk_sim.return %result : i64
    }

    obelisk_sim.func @i64_to_f64(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i64 {obelisk_sim.capture_kind = 1 : i32}) -> f64
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = arith.bitcast %value : i64 to f64
      obelisk_sim.return %result : f64
    }

    obelisk_sim.func @f32_to_i32(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: f32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %result = arith.bitcast %value : f32 to i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func @i32_to_f32(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> f32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %result = arith.bitcast %value : i32 to f32
      obelisk_sim.return %result : f32
    }
  }
}

// Opcode 59 is the append-only scalar integer/float bitcast instruction.
// Each function contains one bitcast, so all four direction/width variants
// must survive instruction selection.
// CHECK-COUNT-4: opcode=59 flags=0
