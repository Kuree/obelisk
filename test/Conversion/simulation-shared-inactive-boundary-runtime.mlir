// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe > %t.out 2> %t.diag
// RUN: FileCheck %s < %t.out
// RUN: FileCheck %s --check-prefix=TIERS < %t.diag
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o0.o
// RUN: %llvm_dist/bin/clang++ %t.o0.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.o0.exe > %t.o0.out 2> %t.o0.diag
// RUN: FileCheck %s < %t.o0.out
// RUN: FileCheck %s --check-prefix=TIERS < %t.o0.diag
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe | FileCheck %s
// #0 is a local Inactive boundary. It must not commit an earlier NBA,
// replay the Active peer, or hand the complete plan to another scheduler.
// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
// TIERS: aot_node_executions=5
// TIERS-SAME: aot_fallbacks=0
// CHECK: active peer
// CHECK-NEXT: inactive value=0
// CHECK-NEXT: later value=1
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 2 : i32
} {
  obelisk_sim.design @boundary {
    obelisk_sim.scope.decl 0 hierarchy "boundary"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "boundary.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "boundary.worker"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "boundary.peer"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "boundary.value"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zero to %ref : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %worker = obelisk_sim.spawn @worker(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      %peer = obelisk_sim.spawn @peer(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @worker(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %one to %ref : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %zero = obelisk_sim.time.constant 0
      obelisk_sim.suspend.delay %zero to ^inactive {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^inactive:
      %current = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %value = obelisk_sim.ref.load %current : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %channel = arith.constant 1 : i32
      %message = obelisk_sim.bytes.constant "inactive value=%0d"
      obelisk_sim.display %ctx to %channel(%message, %value) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^later {site = #obelisk_sim.continuation<id = 2>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^later:
      %final = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %committed = obelisk_sim.ref.load %final : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %stdout = arith.constant 1 : i32
      %format = obelisk_sim.bytes.constant "later value=%0d"
      obelisk_sim.display %ctx to %stdout(%format, %committed) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
    obelisk_sim.func @peer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %stdout = arith.constant 1 : i32
      %message = obelisk_sim.bytes.constant "active peer"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
