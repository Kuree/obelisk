// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAIN < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=RESUMED < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=SCALAR < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O2>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe +n=17 | FileCheck %s --check-prefix=FOUND
// RUN: %t.exe | FileCheck %s --check-prefix=MISSING

// Block-local plusarg strings are native-AOT eligible but still need a live
// managed lane. Exercise both a plain startup actor and a resumed coroutine;
// the root itself never creates a string that could accidentally supply one.
// PLAN: __obelisk_aot_schedule_plan_v1
// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot
// PLAIN-LABEL: llvm.mlir.global external constant @initial.__obelisk_process_descriptor()
// PLAIN-NOT: llvm.insertvalue {{.*}}[2] : !llvm.struct<(struct
// PLAIN: llvm.return
// RESUMED-LABEL: llvm.mlir.global external constant @resumed.__obelisk_process_descriptor()
// RESUMED-NOT: llvm.insertvalue {{.*}}[2] : !llvm.struct<(struct
// RESUMED: llvm.return
// SCALAR-LABEL: llvm.mlir.global external constant @scalar.__obelisk_process_descriptor()
// SCALAR: llvm.insertvalue {{.*}}[1] : !llvm.struct<(struct
// SCALAR: %[[FLAG:.*]] = llvm.mlir.constant(1 : i32)
// SCALAR-NEXT: llvm.insertvalue %[[FLAG]], {{.*}}[2] : !llvm.struct<(struct
// FOUND: initial found=1 value=17
// FOUND-NEXT: resumed found=1 value=17
// MISSING: initial found=0 value=99
// MISSING-NEXT: resumed found=0 value=99
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 2 : i32
} {
  obelisk_sim.design @plusargs {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "initial"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "resumed"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "scalar"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = obelisk_sim.spawn @initial(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      %b = obelisk_sim.spawn @resumed(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      %c = obelisk_sim.spawn @scalar(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @scalar(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %prefix = obelisk_sim.string.literal "n="
      %tail, %found = "obelisk_sim.plusarg.value"(%ctx, %prefix) : (!obelisk_sim.context, !obelisk_sim.string) -> (!obelisk_sim.string, i32)
      %parsed = obelisk_sim.plusarg.parse_logic %tail {radix = 10 : i32} : (!obelisk_sim.string) -> !obelisk_sim.logic<32>
      %zero = arith.constant 0 : i32
      %matched = arith.cmpi ne, %found, %zero : i32
      %default = obelisk_sim.logic.constant 99 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %value = arith.select %matched, %parsed, %default : !obelisk_sim.logic<32>
      %format = obelisk_sim.bytes.constant "initial found=%0d value=%0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%format, %found, %value) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, i32, !obelisk_sim.logic<32>
      obelisk_sim.return
    }
    obelisk_sim.func @resumed(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      %prefix = obelisk_sim.string.literal "n="
      %tail, %found = "obelisk_sim.plusarg.value"(%ctx, %prefix) : (!obelisk_sim.context, !obelisk_sim.string) -> (!obelisk_sim.string, i32)
      %parsed = obelisk_sim.plusarg.parse_logic %tail {radix = 10 : i32} : (!obelisk_sim.string) -> !obelisk_sim.logic<32>
      %zero = arith.constant 0 : i32
      %matched = arith.cmpi ne, %found, %zero : i32
      %default = obelisk_sim.logic.constant 99 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %value = arith.select %matched, %parsed, %default : !obelisk_sim.logic<32>
      %format = obelisk_sim.bytes.constant "resumed found=%0d value=%0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%format, %found, %value) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, i32, !obelisk_sim.logic<32>
      obelisk_sim.return
    }
  }
}
