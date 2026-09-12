// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=BYTECODE

module attributes {
  obelisk.coverage.line_point_count = 10 : i64,
  llvm.data_layout = "e-m:e-p:64:64-i64:64-i32:32-i16:16-i8:8-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @coverage_point {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %true = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %true[9] : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}

// NATIVE-NOT: llvm.call @obelisk_rt_v1_gc_current_lane
// NATIVE: %[[ENABLED:.*]] = llvm.zext {{.*}} : i1 to i32
// NATIVE: %[[POINT:.*]] = llvm.mlir.constant(9 : i64) : i64
// NATIVE: %[[STATUS:.*]] = llvm.call @obelisk_rt_v1_coverage_point_hit({{.*}}, %[[POINT]], %[[ENABLED]]) : (!llvm.ptr, i64, i32) -> i32
// NATIVE: %[[OK:.*]] = llvm.icmp "eq" %[[STATUS]], {{.*}} : i32
// NATIVE: llvm.cond_br %[[OK]],
// NATIVE-NOT: llvm.call @obelisk_rt_v1_gc_current_lane
// BYTECODE: intrinsic {{.*}}id=0x00010468 inputs=2 outputs=0 flags=0
