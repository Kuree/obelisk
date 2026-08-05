// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s \
// RUN:       --implicit-check-not='guarded_blocking{{.*}}__obelisk_two_state'

// Path-sensitive promotion may dry-run reads and fixed NBA staging, because
// the combined barrier is still the publication boundary. A blocking store
// is deliberately ineligible: its X/Z value could otherwise reach a
// downstream two-state owner before the dispatcher reports its fallback.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @path_promotion {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "path.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "path.clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "path.guarded_nba"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "path.guarded_blocking"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %source = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %nba_destination = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %blocking_destination = obelisk_sim.context.storage %ctx[3] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %clock_process = obelisk_sim.spawn @clock(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %nba_process = obelisk_sim.spawn @guarded_nba(
          %ctx, %clock, %source, %nba_destination) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %blocking_process = obelisk_sim.spawn @guarded_blocking(
          %ctx, %source, %source, %blocking_destination) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #obelisk_sim.continuation<id = 1>,
           timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clock :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clock : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }

    obelisk_sim.func @guarded_nba(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %destination: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume
          {site = #obelisk_sim.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %value = obelisk_sim.ref.load %source :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %take_fast_path = obelisk_sim.logic.compare case_eq %value, %zero :
          (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      cf.cond_br %take_fast_path, ^publish, ^checkpoint
    ^publish:
      obelisk_sim.nba.enqueue %value to %destination :
          (!obelisk_sim.logic<1>,
           !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      cf.br ^wait
    ^checkpoint:
      %stdout = arith.constant -2147483647 : i32
      %message = obelisk_sim.bytes.constant "nba checkpoint"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      cf.br ^wait
    }

    obelisk_sim.func @guarded_blocking(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %destination: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume
          {site = #obelisk_sim.continuation<id = 3>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %value = obelisk_sim.ref.load %source :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %take_fast_path = obelisk_sim.logic.compare case_eq %value, %zero :
          (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      cf.cond_br %take_fast_path, ^publish, ^checkpoint
    ^publish:
      obelisk_sim.ref.store %value to %destination :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    ^checkpoint:
      %stdout = arith.constant -2147483647 : i32
      %message = obelisk_sim.bytes.constant "blocking checkpoint"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      cf.br ^wait
    }
  }
}

// CHECK-DAG: llvm.func @guarded_nba{{.*}}__obelisk_path_known
// CHECK-NOT: llvm.func @guarded_blocking{{.*}}__obelisk_path_known
// CHECK-LABEL: llvm.func @__obelisk_eval_fast_coordinator_two_state_v1
// CHECK: %[[HANDOFF_STATUS:.*]] = llvm.call @__obelisk_eval_four_state_nba_handoff_v1
// CHECK-NEXT: llvm.return %[[HANDOFF_STATUS]] : i32
// CHECK-LABEL: llvm.func @__obelisk_eval_four_state_nba_handoff_v1
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: %[[COMMIT_STATUS:.*]] = llvm.call @__obelisk_aot_static_nba_commit_v1
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: %[[COMMIT_OK:.*]] = llvm.icmp "eq" %[[COMMIT_STATUS]], %{{.*}} : i32
// CHECK-NEXT: llvm.cond_br %[[COMMIT_OK]], ^[[INVALIDATE:bb[0-9]+]], ^[[COMPLETE:bb[0-9]+]]
// CHECK: ^[[INVALIDATE]]:
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.call @__obelisk_eval_promotion_invalidate_v1
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: %[[INGRESS:.*]] = llvm.load
// CHECK: %[[INGRESS_EMPTY:.*]] = llvm.icmp "eq" %[[INGRESS]], %{{.*}} : i64
// CHECK-NEXT: llvm.cond_br %[[INGRESS_EMPTY]], ^[[COMPLETE]], ^[[SETTLE:bb[0-9]+]]
// CHECK: ^[[COMPLETE]]:
// CHECK-NEXT: llvm.return %[[COMMIT_STATUS]] : i32
// CHECK: ^[[SETTLE]]:
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: %[[SETTLE_STATUS:.*]] = llvm.call @__obelisk_eval_fast_coordinator_v1
// CHECK-NEXT: llvm.return %[[SETTLE_STATUS]] : i32
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_snapshot_v1
// CHECK-LABEL: llvm.func @__obelisk_eval_path_dispatch_v1_
// CHECK: llvm.call @guarded_nba{{.*}}__obelisk_path_known
// CHECK: llvm.cond_br
// CHECK: llvm.call @guarded_nba{{.*}}__obelisk_two_state
// CHECK: llvm.mlir.addressof @__obelisk_eval_step_four_state_fallback_v1
// CHECK: llvm.call @guarded_nba.__obelisk_eval_body_0
// CHECK: llvm.return
