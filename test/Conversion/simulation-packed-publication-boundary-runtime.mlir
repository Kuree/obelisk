// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.native.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.native.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.native.o
// RUN: %llvm_dist/bin/clang++ %t.native.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.native.exe
// RUN: %t.native.exe | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe | FileCheck %s
// Exercise the shared publication boundary in native fragments AND genuinely
// required bytecode. Changing either neighboring bit must not wake the partial
// [64:1] change wait. Bit 64 crosses a word boundary; 1->X is a negedge and
// X->1 is a posedge (IEEE 1800-2023 9.4.2). Vector changes coalesce per wait.
// CHECK: counts 4 2 2
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.native_scheduler = 2 : i32} {
  obelisk_sim.design @boundary {
    obelisk_sim.scope.decl 0 hierarchy "boundary"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "boundary.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "boundary.watch0"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "boundary.watch1"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "boundary.watch2"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<130> design hierarchy "boundary.data"
    obelisk_sim.storage.decl 1 in 0 : i32 design hierarchy "boundary.count1"
    obelisk_sim.storage.decl 2 in 0 : i32 design hierarchy "boundary.count2"
    obelisk_sim.storage.decl 3 in 0 : i32 design hierarchy "boundary.count3"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %data = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<130>>
      %zero = obelisk_sim.logic.constant 0 : i130, 0 : i130 : !obelisk_sim.logic<130>
      %z = arith.constant 0 : i32
      obelisk_sim.ref.store %zero to %data : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      %c1 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %z to %c1 : i32, !obelisk_sim.ref<i32>
      %p1 = obelisk_sim.spawn @watch0(%ctx, %data) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.process
      %c2 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %z to %c2 : i32, !obelisk_sim.ref<i32>
      %p2 = obelisk_sim.spawn @watch1(%ctx, %data) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.process
      %c3 = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %z to %c3 : i32, !obelisk_sim.ref<i32>
      %p3 = obelisk_sim.spawn @watch2(%ctx, %data) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.process
      cf.br ^step0
    ^step0:
      %d0 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %d0 to ^store0 {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^store0:
      %v0 = obelisk_sim.logic.constant 1 : i130, 0 : i130 : !obelisk_sim.logic<130>
      obelisk_sim.ref.store %v0 to %data : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      cf.br ^step1
    ^step1:
      %d1 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %d1 to ^store1 {site = #obelisk_sim.continuation<id = 2>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^store1:
      %v1 = obelisk_sim.logic.constant 18446744073709551617 : i130, 0 : i130 : !obelisk_sim.logic<130>
      obelisk_sim.ref.store %v1 to %data : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      cf.br ^step2
    ^step2:
      %d2 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %d2 to ^store2 {site = #obelisk_sim.continuation<id = 3>, timing = #obelisk_sim.timing_site<id = 2, kind = calendar>}
    ^store2:
      %v2 = obelisk_sim.logic.constant 55340232221128654849 : i130, 0 : i130 : !obelisk_sim.logic<130>
      obelisk_sim.ref.store %v2 to %data : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      cf.br ^step3
    ^step3:
      %d3 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %d3 to ^store3 {site = #obelisk_sim.continuation<id = 4>, timing = #obelisk_sim.timing_site<id = 3, kind = calendar>}
    ^store3:
      %v3 = obelisk_sim.logic.constant 55340232221128654849 : i130, 18446744073709551616 : i130 : !obelisk_sim.logic<130>
      obelisk_sim.ref.store %v3 to %data : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      cf.br ^step4
    ^step4:
      %d4 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %d4 to ^store4 {site = #obelisk_sim.continuation<id = 5>, timing = #obelisk_sim.timing_site<id = 4, kind = calendar>}
    ^store4:
      %v4 = obelisk_sim.logic.constant 55340232221128654849 : i130, 0 : i130 : !obelisk_sim.logic<130>
      obelisk_sim.ref.store %v4 to %data : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      cf.br ^step5
    ^step5:
      %d5 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %d5 to ^store5 {site = #obelisk_sim.continuation<id = 6>, timing = #obelisk_sim.timing_site<id = 5, kind = calendar>}
    ^store5:
      %v5 = obelisk_sim.logic.constant 36893488147419103233 : i130, 0 : i130 : !obelisk_sim.logic<130>
      obelisk_sim.ref.store %v5 to %data : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      cf.br ^finish
    ^finish:
      %last = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %last to ^print {site = #obelisk_sim.continuation<id = 7>, timing = #obelisk_sim.timing_site<id = 6, kind = calendar>}
    ^print:
      %r1 = obelisk_sim.ref.load %c1 : !obelisk_sim.ref<i32> -> i32
      %r2 = obelisk_sim.ref.load %c2 : !obelisk_sim.ref<i32> -> i32
      %r3 = obelisk_sim.ref.load %c3 : !obelisk_sim.ref<i32> -> i32
      %fmt = obelisk_sim.bytes.constant "counts %0d %0d %0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %r1, %r2, %r3) newline = true radix = 10 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, i32, i32, i32
      obelisk_sim.finish %ctx, %z
      obelisk_sim.return
    }
    obelisk_sim.func @watch0(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !obelisk_sim.ref<!obelisk_sim.logic<130>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %part = obelisk_sim.ref.extract %data from 1 : !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.ref<!obelisk_sim.logic<64>>
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %part to ^count {site = #obelisk_sim.continuation<id = 8>} : !obelisk_sim.ref<!obelisk_sim.logic<64>>
    ^count:
      %counter = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i32>
      %old = obelisk_sim.ref.load %counter : !obelisk_sim.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      obelisk_sim.ref.store %next to %counter : i32, !obelisk_sim.ref<i32>
      cf.br ^wait
    }
    obelisk_sim.func @watch1(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !obelisk_sim.ref<!obelisk_sim.logic<130>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %part = obelisk_sim.ref.extract %data from 64 : !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %part to ^count {site = #obelisk_sim.continuation<id = 9>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^count:
      %counter = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<i32>
      %old = obelisk_sim.ref.load %counter : !obelisk_sim.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      obelisk_sim.ref.store %next to %counter : i32, !obelisk_sim.ref<i32>
      cf.br ^wait
    }
    obelisk_sim.func @watch2(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !obelisk_sim.ref<!obelisk_sim.logic<130>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %part = obelisk_sim.ref.extract %data from 64 : !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge negedge %part to ^count {site = #obelisk_sim.continuation<id = 10>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^count:
      %counter = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<i32>
      %old = obelisk_sim.ref.load %counter : !obelisk_sim.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      obelisk_sim.ref.store %next to %counter : i32, !obelisk_sim.ref<i32>
      cf.br ^wait
    }
  }
}
