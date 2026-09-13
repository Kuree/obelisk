// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.native.mlir
// RUN: FileCheck %s --check-prefix=PLAN --implicit-check-not=': !llvm.ptr -> i65536' < %t.native.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.native.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llvm_dist/bin/llc -O0 -filetype=obj -relocation-model=pic -o %t.native.o
// RUN: %llvm_dist/bin/clang++ %t.native.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.native.exe
// RUN: %t.native.exe | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe | FileCheck %s
// Check full vector equality, two distinct low limbs, and the far end of both
// planes. Force native -O0 execution so constant folding cannot hide codegen
// errors; compare against the true bytecode tier and literal expected values.
// PLAN: llvm.xor {{.*}} : vector<1024xi64>
// CHECK: limbs fffffffffffffffe fffffffffffffffd ffffffffffffffff
// CHECK-NEXT: words 1 1 1 0 1
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk_sim.design @wide_words {
    obelisk_sim.scope.decl 0 hierarchy "wide_words"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<65536> design hierarchy "wide_words.a"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<65536> design hierarchy "wide_words.b"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "wide_words.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "wide_words.test"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<65536>>
      %b = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<65536>>
      %p = obelisk_sim.spawn @test(%ctx, %a, %b) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<65536>>, !obelisk_sim.ref<!obelisk_sim.logic<65536>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @test(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<65536>> {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<65536>> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %pattern = obelisk_sim.logic.constant 36893488147419103233 : i65536, 0 : i65536 : !obelisk_sim.logic<65536>
      %zero = obelisk_sim.logic.constant 0 : i65536, 0 : i65536 : !obelisk_sim.logic<65536>
      %ones = obelisk_sim.logic.constant -1 : i65536, 0 : i65536 : !obelisk_sim.logic<65536>
      %allx = obelisk_sim.logic.constant 0 : i65536, -1 : i65536 : !obelisk_sim.logic<65536>
      %allz = obelisk_sim.logic.constant -1 : i65536, -1 : i65536 : !obelisk_sim.logic<65536>
      obelisk_sim.ref.store %pattern to %a : !obelisk_sim.logic<65536>, !obelisk_sim.ref<!obelisk_sim.logic<65536>>
      %loaded = obelisk_sim.ref.load %a : !obelisk_sim.ref<!obelisk_sim.logic<65536>> -> !obelisk_sim.logic<65536>
      %inverse = obelisk_sim.logic.binary xnor %loaded, %zero : !obelisk_sim.logic<65536>
      obelisk_sim.ref.store %inverse to %b : !obelisk_sim.logic<65536>, !obelisk_sim.ref<!obelisk_sim.logic<65536>>
      %read = obelisk_sim.ref.load %b : !obelisk_sim.ref<!obelisk_sim.logic<65536>> -> !obelisk_sim.logic<65536>
      %expected = obelisk_sim.logic.constant -36893488147419103234 : i65536, 0 : i65536 : !obelisk_sim.logic<65536>
      %ok = obelisk_sim.logic.compare case_eq %read, %expected : (!obelisk_sim.logic<65536>, !obelisk_sim.logic<65536>) -> i1
      %low = obelisk_sim.ref.extract %b from 0 : !obelisk_sim.ref<!obelisk_sim.logic<65536>> -> !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %second = obelisk_sim.ref.extract %b from 64 : !obelisk_sim.ref<!obelisk_sim.logic<65536>> -> !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %last = obelisk_sim.ref.extract %b from 65472 : !obelisk_sim.ref<!obelisk_sim.logic<65536>> -> !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %vlow = obelisk_sim.ref.load %low : !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %vsecond = obelisk_sim.ref.load %second : !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %vlast = obelisk_sim.ref.load %last : !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %limbs = obelisk_sim.bytes.constant "limbs %h %h %h"
      %fd = arith.constant 1 : i32
      obelisk_sim.display %ctx to %fd(%limbs, %vlow, %vsecond, %vlast) newline = true radix = 10 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<64>, !obelisk_sim.logic<64>, !obelisk_sim.logic<64>
      obelisk_sim.ref.store %allz to %a : !obelisk_sim.logic<65536>, !obelisk_sim.ref<!obelisk_sim.logic<65536>>
      %z = obelisk_sim.ref.load %a : !obelisk_sim.ref<!obelisk_sim.logic<65536>> -> !obelisk_sim.logic<65536>
      %x = obelisk_sim.logic.binary xor %z, %ones : !obelisk_sim.logic<65536>
      obelisk_sim.ref.store %x to %b : !obelisk_sim.logic<65536>, !obelisk_sim.ref<!obelisk_sim.logic<65536>>
      %readx = obelisk_sim.ref.load %b : !obelisk_sim.ref<!obelisk_sim.logic<65536>> -> !obelisk_sim.logic<65536>
      %okx = obelisk_sim.logic.compare case_eq %readx, %allx : (!obelisk_sim.logic<65536>, !obelisk_sim.logic<65536>) -> i1
      %different = obelisk_sim.logic.compare case_ne %readx, %ones : (!obelisk_sim.logic<65536>, !obelisk_sim.logic<65536>) -> i1
      %notEqual = obelisk_sim.logic.compare case_eq %loaded, %zero : (!obelisk_sim.logic<65536>, !obelisk_sim.logic<65536>) -> i1
      %unequal = obelisk_sim.logic.compare case_ne %loaded, %zero : (!obelisk_sim.logic<65536>, !obelisk_sim.logic<65536>) -> i1
      %text = obelisk_sim.bytes.constant "words %0d %0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%text, %ok, %okx, %different, %notEqual, %unequal) newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0] : !obelisk_sim.bytes, i1, i1, i1, i1, i1
      obelisk_sim.return
    }
  }
}
