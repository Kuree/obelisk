// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.native.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.native.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llc -filetype=obj -relocation-model=pic -o %t.native.o
// RUN: %llvm_dist/bin/clang++ %t.native.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.native.exe
// RUN: %t.native.exe | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe | FileCheck %s
// Exercise the shared publication boundary in native fragments AND genuinely
// required bytecode. Changing either neighboring bit must not wake the partial
// [64:1] change wait. Bit 64 crosses a word boundary; 1->X is a negedge and
// X->1 is a posedge (IEEE 1800-2023 9.4.2). Vector changes coalesce per wait.
// CHECK: counts 4 2 2
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 2 : i32} {
  simulation.design @boundary {
    simulation.scope.decl 0 hierarchy "boundary"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "boundary.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "boundary.watch0"
    simulation.code_unit.decl 3 in 0 always hierarchy "boundary.watch1"
    simulation.code_unit.decl 4 in 0 always hierarchy "boundary.watch2"
    simulation.storage.decl 0 in 0 : !simulation.logic<130> design hierarchy "boundary.data"
    simulation.storage.decl 1 in 0 : i32 design hierarchy "boundary.count1"
    simulation.storage.decl 2 in 0 : i32 design hierarchy "boundary.count2"
    simulation.storage.decl 3 in 0 : i32 design hierarchy "boundary.count3"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %data = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<130>>
      %zero = simulation.logic.constant 0 : i130, 0 : i130 : !simulation.logic<130>
      %z = arith.constant 0 : i32
      simulation.ref.store %zero to %data : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      %c1 = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      simulation.ref.store %z to %c1 : i32, !simulation.ref<i32>
      %p1 = simulation.spawn @watch0(%ctx, %data) : !simulation.context, !simulation.ref<!simulation.logic<130>> -> !simulation.process
      %c2 = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      simulation.ref.store %z to %c2 : i32, !simulation.ref<i32>
      %p2 = simulation.spawn @watch1(%ctx, %data) : !simulation.context, !simulation.ref<!simulation.logic<130>> -> !simulation.process
      %c3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      simulation.ref.store %z to %c3 : i32, !simulation.ref<i32>
      %p3 = simulation.spawn @watch2(%ctx, %data) : !simulation.context, !simulation.ref<!simulation.logic<130>> -> !simulation.process
      cf.br ^step0
    ^step0:
      %d0 = simulation.time.constant 1
      simulation.suspend.delay %d0 to ^store0 {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^store0:
      %v0 = simulation.logic.constant 1 : i130, 0 : i130 : !simulation.logic<130>
      simulation.ref.store %v0 to %data : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      cf.br ^step1
    ^step1:
      %d1 = simulation.time.constant 1
      simulation.suspend.delay %d1 to ^store1 {site = #schedule.continuation<id = 2>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^store1:
      %v1 = simulation.logic.constant 18446744073709551617 : i130, 0 : i130 : !simulation.logic<130>
      simulation.ref.store %v1 to %data : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      cf.br ^step2
    ^step2:
      %d2 = simulation.time.constant 1
      simulation.suspend.delay %d2 to ^store2 {site = #schedule.continuation<id = 3>, timing = #schedule.timing_site<id = 2, kind = calendar>}
    ^store2:
      %v2 = simulation.logic.constant 55340232221128654849 : i130, 0 : i130 : !simulation.logic<130>
      simulation.ref.store %v2 to %data : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      cf.br ^step3
    ^step3:
      %d3 = simulation.time.constant 1
      simulation.suspend.delay %d3 to ^store3 {site = #schedule.continuation<id = 4>, timing = #schedule.timing_site<id = 3, kind = calendar>}
    ^store3:
      %v3 = simulation.logic.constant 55340232221128654849 : i130, 18446744073709551616 : i130 : !simulation.logic<130>
      simulation.ref.store %v3 to %data : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      cf.br ^step4
    ^step4:
      %d4 = simulation.time.constant 1
      simulation.suspend.delay %d4 to ^store4 {site = #schedule.continuation<id = 5>, timing = #schedule.timing_site<id = 4, kind = calendar>}
    ^store4:
      %v4 = simulation.logic.constant 55340232221128654849 : i130, 0 : i130 : !simulation.logic<130>
      simulation.ref.store %v4 to %data : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      cf.br ^step5
    ^step5:
      %d5 = simulation.time.constant 1
      simulation.suspend.delay %d5 to ^store5 {site = #schedule.continuation<id = 6>, timing = #schedule.timing_site<id = 5, kind = calendar>}
    ^store5:
      %v5 = simulation.logic.constant 36893488147419103233 : i130, 0 : i130 : !simulation.logic<130>
      simulation.ref.store %v5 to %data : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      cf.br ^finish
    ^finish:
      %last = simulation.time.constant 1
      simulation.suspend.delay %last to ^print {site = #schedule.continuation<id = 7>, timing = #schedule.timing_site<id = 6, kind = calendar>}
    ^print:
      %r1 = simulation.ref.load %c1 : !simulation.ref<i32> -> i32
      %r2 = simulation.ref.load %c2 : !simulation.ref<i32> -> i32
      %r3 = simulation.ref.load %c3 : !simulation.ref<i32> -> i32
      %fmt = simulation.bytes.constant "counts %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %r1, %r2, %r3) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i32, i32, i32
      simulation.finish %ctx, %z
      simulation.return
    }
    simulation.func @watch0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !simulation.ref<!simulation.logic<130>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %part = simulation.ref.extract %data from 1 : !simulation.ref<!simulation.logic<130>> -> !simulation.ref<!simulation.logic<64>>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %part to ^count {site = #schedule.continuation<id = 8>} : !simulation.ref<!simulation.logic<64>>
    ^count:
      %counter = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %old = simulation.ref.load %counter : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %counter : i32, !simulation.ref<i32>
      cf.br ^wait
    }
    simulation.func @watch1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !simulation.ref<!simulation.logic<130>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %part = simulation.ref.extract %data from 64 : !simulation.ref<!simulation.logic<130>> -> !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %part to ^count {site = #schedule.continuation<id = 9>} : !simulation.ref<!simulation.logic<1>>
    ^count:
      %counter = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      %old = simulation.ref.load %counter : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %counter : i32, !simulation.ref<i32>
      cf.br ^wait
    }
    simulation.func @watch2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !simulation.ref<!simulation.logic<130>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %part = simulation.ref.extract %data from 64 : !simulation.ref<!simulation.logic<130>> -> !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %part to ^count {site = #schedule.continuation<id = 10>} : !simulation.ref<!simulation.logic<1>>
    ^count:
      %counter = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %old = simulation.ref.load %counter : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %counter : i32, !simulation.ref<i32>
      cf.br ^wait
    }
  }
}
