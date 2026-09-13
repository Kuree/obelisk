// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.native.mlir
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.native.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.native.o
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.native.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.native.exe
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.native.exe | FileCheck %s
// RUN: %t.bytecode.exe | FileCheck %s

// A never-instantiated covergroup still declares type_option storage. Dumpvars
// requests reflection even with VPI off. The options' physical span is 256
// bits, not the 195-bit sum of the fields: real and string slots are aligned.
// Exercise that exact lowering boundary without a frontend/SV fixture.
// CHECK: reached
!options = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "weight", type = i32, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "goal", type = i32, ordinal = 1, packedOffset = 0>,
  #obelisk_sim.field<name = "comment", type = !obelisk_sim.string, ordinal = 2, packedOffset = 0>,
  #obelisk_sim.field<name = "strobe", type = i1, ordinal = 3, packedOffset = 0>,
  #obelisk_sim.field<name = "merge_instances", type = i1, ordinal = 4, packedOffset = 0>,
  #obelisk_sim.field<name = "distribute_first", type = i1, ordinal = 5, packedOffset = 0>,
  #obelisk_sim.field<name = "real_interval", type = f64, ordinal = 6, packedOffset = 0>]>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @padded {
    obelisk_sim.scope.decl 0 hierarchy "padded"
    obelisk_sim.storage.decl 0 in 0 : !options design hierarchy "padded.cg.type_option"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "padded.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "padded.initial"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %p = obelisk_sim.spawn @initial(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %path = obelisk_sim.bytes.constant "/dev/null"
      %scope = obelisk_sim.bytes.constant ""
      %levels = arith.constant 0 : i64
      obelisk_sim.dump.open %ctx, %path : (!obelisk_sim.context, !obelisk_sim.bytes) -> ()
      obelisk_sim.dump.vars %ctx, %levels, %scope : (!obelisk_sim.context, i64, !obelisk_sim.bytes) -> ()
      %message = obelisk_sim.bytes.constant "reached"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
