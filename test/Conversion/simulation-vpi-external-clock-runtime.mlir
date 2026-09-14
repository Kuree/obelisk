// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang -I%S/../../runtime/include -I%resource_dir/include -c %S/Inputs/vpi-external-clock.c -o %t.helper.o
// RUN: %llvm_dist/bin/clang++ %t.o %t.helper.o -Wl,--wrap=obelisk_rt_v1_scheduler_run -Wl,--wrap=obelisk_rt_v1_scheduler_run_aot %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe > %t.out 2> %t.diag
// RUN: FileCheck %s < %t.out
// RUN: FileCheck %s --check-prefix=TIERS < %t.diag
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o0.o
// RUN: %llvm_dist/bin/clang++ %t.o0.o %t.helper.o -Wl,--wrap=obelisk_rt_v1_scheduler_run -Wl,--wrap=obelisk_rt_v1_scheduler_run_aot %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.o0.exe > %t.o0.out 2> %t.o0.diag
// RUN: FileCheck %s < %t.o0.out
// RUN: FileCheck %s --check-prefix=TIERS < %t.o0.diag
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=full require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %t.helper.o -Wl,--wrap=obelisk_rt_v1_scheduler_run -Wl,--wrap=obelisk_rt_v1_scheduler_run_aot %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe | FileCheck %s
// The model has no internal clock process. Every edge is a VPI deposit.
// PLAN: llvm.func @__obelisk_eval_fast_coordinator
// TIERS: aot_node_executions=2
// TIERS-SAME: aot_nba_stages=0
// TIERS-SAME: aot_fallbacks=0
// CHECK: external clock count=1000
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @external_clock {
    obelisk_sim.scope.decl 0 hierarchy "external_clock"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "external_clock.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "external_clock.counter"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "external_clock.clk"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design hierarchy "external_clock.count"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %count = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %zero32 = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      obelisk_sim.ref.store %zero to %clk : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero32 to %count : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %process = obelisk_sim.spawn @counter(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @counter(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^step {site = #obelisk_sim.continuation<id = 1>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^step:
      %count = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %old = obelisk_sim.ref.load %count : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %one = obelisk_sim.logic.constant 1 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %next = obelisk_sim.logic.binary add %old, %one : !obelisk_sim.logic<32>
      obelisk_sim.nba.enqueue %next to %count : (!obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      cf.br ^wait
    }
  }
}
