// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe > %t.out 2> %t.diag
// RUN: FileCheck %s < %t.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

// Reset changes in a finite startup process, outside the periodic clock's
// dependency closure. Promoted two-state port bodies must still publish the
// change through both inverters, including when invoked at a checkpoint.
// CHECK: reset=1 inverted=0 forwarded=1 sampled=1
// PLAN: __obelisk_periodic_clock_plan_v1
// PLAN-LABEL: llvm.func @first.__obelisk_eval_body_0.__obelisk_two_state_0(
// PLAN: llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// PLAN: llvm.store
// PLAN: llvm.return
// PLAN-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// PLAN-NOT: @__obelisk_eval_function_route_v1_
// PLAN-NOT: @__obelisk_eval_route_promotion_pending_v1
// PLAN: %[[STATUS:.*]] = llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// PLAN-NEXT: {{.*}}llvm.call @__obelisk_eval_route_promotion_boundary_v1(%[[STATUS]])
// DIAG: periodic_preparations={{[1-9][0-9]*}}

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @reset_publication {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0
    obelisk_sim.scope.decl 2 parent 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 1 continuous hierarchy "first"
    obelisk_sim.code_unit.decl 4 in 2 continuous hierarchy "second"
    obelisk_sim.code_unit.decl 5 in 0 always hierarchy "sample"
    obelisk_sim.code_unit.decl 6 in 0 initial hierarchy "observe"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 4 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s0 = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %s1 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %s2 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %s3 = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %s4 = obelisk_sim.context.storage %ctx[4] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %bit = arith.constant false
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %bit to %s0 : i1, !obelisk_sim.ref<i1>
      obelisk_sim.ref.store %zero to %s1 : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero to %s2 : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero to %s3 : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero to %s4 : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %clock = obelisk_sim.spawn @clock(%ctx, %s0) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %first = obelisk_sim.spawn @first(%ctx, %s1, %s2) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %second = obelisk_sim.spawn @second(%ctx, %s2, %s3) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %sample = obelisk_sim.spawn @sample(%ctx, %s0, %s3, %s4) : !obelisk_sim.context, !obelisk_sim.ref<i1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %observe = obelisk_sim.spawn @observe(%ctx, %s1, %s2, %s3, %s4) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %s0: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %s0 : !obelisk_sim.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      obelisk_sim.ref.store %next to %s0 : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
    obelisk_sim.func @first(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %s1: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %s2: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      cf.br ^run
    ^run:
      %old = obelisk_sim.ref.load %s1 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %next = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %next to %s2 {obelisk_sim.continuous_store} : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.suspend.change %s1 to ^run {site = #schedule.continuation<id = 3>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func @second(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %s2: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %s3: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      cf.br ^run
    ^run:
      %old = obelisk_sim.ref.load %s2 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %next = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %next to %s3 {obelisk_sim.continuous_store} : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.suspend.change %s2 to ^run {site = #schedule.continuation<id = 4>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func @sample(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %s0: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %s3: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %s4: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %s0 to ^run {site = #schedule.continuation<id = 5>} : !obelisk_sim.ref<i1>
    ^run:
      %value = obelisk_sim.ref.load %s3 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %value to %s4 : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      cf.br ^wait
    }
    obelisk_sim.func @observe(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %s1: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %s2: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %s3: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %s4: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %d0 = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %d0 to ^update {site = #schedule.continuation<id = 6>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^update:
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %one to %s1 : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %d1 = obelisk_sim.time.constant 10
      obelisk_sim.suspend.delay %d1 to ^show {site = #schedule.continuation<id = 7>, timing = #schedule.timing_site<id = 2, kind = calendar>}
    ^show:
      %v1 = obelisk_sim.ref.load %s1 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v2 = obelisk_sim.ref.load %s2 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v3 = obelisk_sim.ref.load %s3 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v4 = obelisk_sim.ref.load %s4 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fmt = obelisk_sim.bytes.constant "reset=%b inverted=%b forwarded=%b sampled=%b"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %v1, %v2, %v3, %v4) newline = true radix = 10 flags = [0, 0, 0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      obelisk_sim.finish %ctx, %channel
      obelisk_sim.return
    }
  }
}
