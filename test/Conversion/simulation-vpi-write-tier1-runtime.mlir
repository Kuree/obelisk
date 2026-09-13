// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang -I%S/../../runtime/include -I%resource_dir/include -c %S/Inputs/vpi-write-tier1.c -o %t.helper.o
// RUN: %llvm_dist/bin/clang++ %t.o %t.helper.o -Wl,--wrap=obelisk_rt_v1_scheduler_run_aot -Wl,--wrap=obelisk_rt_v1_display %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe > %t.out 2> %t.diag
// RUN: FileCheck %s < %t.out
// RUN: FileCheck %s --check-prefix=TIERS < %t.diag
// RUN: env OBELISK_TEST_IMMEDIATE_NET_RELEASE=1 %t.exe | FileCheck %s
// A writer arrives only AFTER 500 Tier-1 clock activations. Deposits must
// update the canonical planes; force must survive NBA writes; variable release
// retains the forced value until the next assignment. Depositing X invalidates
// two-state promotion, and a later known deposit permits recovery.
// PLAN-DAG: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// PLAN-DAG: llvm.func @__obelisk_eval_fast_coordinator_hybrid_v1
// Both long, writer-free intervals must use Tier 1. Runtime work stays
// bounded by the seven actual mutations, including recovery after release.
// TIERS: scheduler_iterations=18
// TIERS-SAME: aot_node_executions=17
// CHECK: before 0 000001f4
// CHECK-NEXT: after 0 00000029
// CHECK-NEXT: before 1 0000002a
// CHECK-NEXT: after 1 00000007
// CHECK-NEXT: before 2 00000007
// CHECK-NEXT: after 2 00000007
// CHECK-NEXT: before 3 00000008
// CHECK-NEXT: after 3 xxxxxxxx
// CHECK-NEXT: before 4 xxxxxxxx
// CHECK-NEXT: after 4 00000000
// CHECK-NEXT: before 5 00000001
// CHECK-NEXT: after 5 00000001 net=00000037
// CHECK-NEXT: before 6 00000002
// CHECK-NEXT: after 6 00000002 net=00000002
// CHECK-NEXT: done 000001f6 net=000001f6

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @vpi_write {
    obelisk_sim.scope.decl 0 hierarchy "vpi_write"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "vpi_write.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "vpi_write.clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "vpi_write.writer"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "vpi_write.check"
    obelisk_sim.code_unit.decl 5 in 0 always hierarchy "vpi_write.drive"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "vpi_write.clk"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design hierarchy "vpi_write.q"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<32> design hierarchy "vpi_write.net"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<32> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %q = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %zero32 = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      obelisk_sim.ref.store %zero to %clk : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero32 to %q : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %initialDriver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<32>>
      obelisk_sim.driver.drive %initialDriver = %zero32 : !obelisk_sim.driver<!obelisk_sim.logic<32>>, !obelisk_sim.logic<32>
      %w = obelisk_sim.spawn @writer(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %t = obelisk_sim.spawn @check(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      %d = obelisk_sim.spawn @drive(%ctx, %q) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      %c = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clk : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
    obelisk_sim.func @writer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^write {site = #obelisk_sim.continuation<id = 2>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^write:
      %q = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %old = obelisk_sim.ref.load %q : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %one = obelisk_sim.logic.constant 1 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %next = obelisk_sim.logic.binary add %old, %one : !obelisk_sim.logic<32>
      obelisk_sim.nba.enqueue %next to %q : (!obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      cf.br ^wait
    }
    obelisk_sim.func @drive(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %q: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %q to ^drive {site = #obelisk_sim.continuation<id = 6>} : !obelisk_sim.ref<!obelisk_sim.logic<32>>
    ^drive:
      %value = obelisk_sim.ref.load %q : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<32>>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<32>>, !obelisk_sim.logic<32>
      cf.br ^wait
    }
    obelisk_sim.func @check(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i32
      %delay = obelisk_sim.time.constant 1000
      obelisk_sim.suspend.delay %delay to ^mutate(%zero : i32) {site = #obelisk_sim.continuation<id = 3>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^mutate(%phase: i32):
      %q = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %before = obelisk_sim.ref.load %q : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %ch = arith.constant 1 : i32
      %fmt = obelisk_sim.bytes.constant "before %0d %08h"
      obelisk_sim.display %ctx to %ch(%fmt, %phase, %before) newline = true radix = 16 flags = [0, 0, 0] : !obelisk_sim.bytes, i32, !obelisk_sim.logic<32>
      %after = obelisk_sim.ref.load %q : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<32>>
      %driven = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %out = obelisk_sim.bytes.constant "after %0d %08h net=%08h"
      obelisk_sim.display %ctx to %ch(%out, %phase, %after, %driven) newline = true radix = 16 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, i32, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %one = arith.constant 1 : i32
      %next = arith.addi %phase, %one : i32
      %five = arith.constant 7 : i32
      %again = arith.cmpi ult, %next, %five : i32
      cf.cond_br %again, ^wait, ^settle
    ^wait:
      %step = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %step to ^mutate(%next : i32) {site = #obelisk_sim.continuation<id = 4>, timing = #obelisk_sim.timing_site<id = 2, kind = calendar>}
    ^settle:
      %long = obelisk_sim.time.constant 1000
      obelisk_sim.suspend.delay %long to ^done {site = #obelisk_sim.continuation<id = 5>, timing = #obelisk_sim.timing_site<id = 3, kind = calendar>}
    ^done:
      %finalRef = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %final = obelisk_sim.ref.load %finalRef : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %channel = arith.constant 1 : i32
      %finalNet = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<32>>
      %finalDriven = obelisk_sim.net.read %finalNet : !obelisk_sim.net<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %format = obelisk_sim.bytes.constant "done %08h net=%08h"
      obelisk_sim.display %ctx to %channel(%format, %final, %finalDriven) newline = true radix = 16 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
