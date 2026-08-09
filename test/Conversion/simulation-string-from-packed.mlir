// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// Keep the packed-to-string operation on the managed runtime path. The runtime
// interprets this value as "BA" by enumerating bytes from most to least
// significant and omitting both zero bytes.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @packed_string {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %packed = arith.constant 1107312896 : i32
      %string = obelisk_sim.string.from_packed %packed :
        (i32) -> !obelisk_sim.string
      %length = obelisk_sim.string.length %string :
        (!obelisk_sim.string) -> i64
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @process(
// CHECK: %[[PACKED:.*]] = llvm.mlir.constant(1107312896 : i32) : i32
// CHECK: llvm.store %[[PACKED]], {{.*}} {alignment = 8 : i64}
// CHECK: %[[WIDTH:.*]] = llvm.mlir.constant(32 : i64) : i64
// CHECK: llvm.call @obelisk_rt_v1_string_from_packed({{.*}}, {{.*}}, {{.*}}, %[[WIDTH]], {{.*}})
// CHECK: llvm.call @obelisk_rt_v1_string_length
// CHECK: llvm.return
