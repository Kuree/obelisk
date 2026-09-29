// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=OUTLINE < %t.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.mlir | opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' -S | FileCheck %s --check-prefix=LLVM

// A checkpoint-capable owner stays in the generated Tier-1/Tier-2 closure on
// its known path.  The unsupported display leaf is fractured into a cold
// Tier-3 callback and queued only after the generated coordinator returns.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @checkpoint_handoff {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "handoff.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "handoff.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "handoff.guarded"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %source = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<1>>
      %destination = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<1>>
      %clock_process = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %guarded_process = simulation.spawn @guarded(
          %ctx, %clock, %source, %destination) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @guarded(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %source: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %destination: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume
          {site = #schedule.continuation<id = 2>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %value = simulation.ref.load %source :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %zero = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      %known_path = simulation.logic.compare case_eq %value, %zero :
          (!simulation.logic<1>, !simulation.logic<1>) -> i1
      cf.cond_br %known_path, ^publish, ^checkpoint
    ^publish:
      simulation.nba.enqueue %value to %destination :
          (!simulation.logic<1>,
           !simulation.ref<!simulation.logic<1>>) -> ()
      cf.br ^wait
    ^checkpoint:
      %unknown = simulation.logic.constant 0 : i1, 1 : i1 :
          !simulation.logic<1>
      simulation.nba.enqueue %unknown to %destination :
          (!simulation.logic<1>,
           !simulation.ref<!simulation.logic<1>>) -> ()
      // Runtime-only preparation for the cold checkpoint must not disqualify
      // the whole periodic eval group. The path probe replaces this complete
      // block with its Tier-3 return before validating its hot closure.
      %now = simulation.time.now %ctx
      %stdout = arith.constant -2147483647 : i32
      %message = simulation.bytes.constant "checkpoint %0t"
      simulation.display %ctx to %stdout(%message, %now) newline = true radix = <decimal>
          flags = [0, 0] : !simulation.bytes, i64
      // Keep an SSA edge from the checkpoint into the tail that the path
      // probe removes.  The probe must detach the complete removed subgraph
      // before clearing this block; otherwise this use becomes dangling.
      cf.br ^checkpoint_tail(%stdout : i32)
    ^checkpoint_tail(%unused: i32):
      cf.br ^wait
    }
  }
}

// The whole-closure certificate scans the exact canonical unknown-plane
// range before clearing this owner's pending bit.
// CHECK-LABEL: llvm.func @__obelisk_eval_kernel_promotion_scan_v1(
// CHECK-SAME: passthrough = ["noinline", "cold"]
// CHECK: llvm.mlir.addressof @__obelisk_eval_kernel_promotion_owners_v1
// CHECK: llvm.mlir.addressof @__obelisk_eval_kernel_promotion_ranges_v1
// CHECK: llvm.mlir.addressof @__obelisk_state_unknown
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i8
// CHECK: llvm.and {{.*}} : i8
// CHECK: llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// CHECK-LABEL: llvm.func @__obelisk_eval_kernel_promotion_ready_v1(
// CHECK-SAME: passthrough = ["alwaysinline"]
// CHECK: llvm.mlir.addressof @__obelisk_eval_kernel_promotion_latched_v1
// CHECK: llvm.call @__obelisk_eval_kernel_promotion_scan_v1

// Path probes and checkpoint continuation remain local to their executor.
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1
// CHECK: llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// CHECK: llvm.call @__obelisk_direct_fragment_{{[0-9]+}}_{{[0-9]+}}.__obelisk_execute
// CHECK-LABEL: llvm.func @__obelisk_eval_checkpoint_body_v1_0(
// CHECK: llvm.call @obelisk_rt_v1_scheduler_time
// CHECK: llvm.call @obelisk_rt_v1_display
// CHECK-LABEL: llvm.func @__obelisk_eval_four_state_fallback_v1_0(
// CHECK-DAG: %[[FALLBACK:.*]] = llvm.mlir.addressof @__obelisk_eval_step_four_state_fallback_v1
// CHECK-DAG: %[[ONE:.*]] = llvm.mlir.constant(1 : i8)
// CHECK: llvm.store %[[ONE]], %[[FALLBACK]]
// CHECK-DAG: %[[ROOTS:.*]] = llvm.mlir.addressof @__obelisk_eval_fast_nba_roots_v1
// CHECK-DAG: %[[ROOT_ZERO:.*]] = llvm.mlir.zero : !llvm.array<1 x i64>
// CHECK: llvm.store %[[ROOT_ZERO]], %[[ROOTS]]
// CHECK: llvm.call @__obelisk_eval_checkpoint_body_v1_0
// CHECK-SAME: no_inline
// CHECK: llvm.call @obelisk_rt_v1_scheduler_queue_aot_checkpoint
// CHECK-LABEL: llvm.func @__obelisk_eval_path_dispatch_v1_0(
// CHECK: llvm.cond_br {{.*}}, ^[[PROMOTED:bb[0-9]+]], ^[[TRANSIENT:bb[0-9]+]]
// CHECK: ^[[TRANSIENT]]:
// CHECK: llvm.call @guarded.__obelisk_eval_body_0.__obelisk_path_known_0
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_time
// CHECK: ^[[PROMOTED]]:
// CHECK: llvm.call @guarded.__obelisk_eval_body_0.__obelisk_checkpoint_path_0
// CHECK-DAG: llvm.mlir.addressof @__obelisk_eval_checkpoint_callback_v1
// CHECK-DAG: llvm.mlir.addressof @__obelisk_eval_four_state_fallback_v1_0
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.return

// The local unknown branch shares its original four-state body; the selected
// two-state branch keeps its normal inlining policy.
// OUTLINE-LABEL: llvm.func @__obelisk_eval_path_dispatch_v1_0(
// OUTLINE: llvm.call @guarded.__obelisk_eval_body_0.__obelisk_two_state_0(
// OUTLINE-NOT: no_inline
// OUTLINE: llvm.return
// OUTLINE: llvm.call @guarded.__obelisk_eval_body_0(
// OUTLINE-SAME: no_inline

// LLVM-LABEL: define {{.*}}@__obelisk_eval_four_state_fallback_v1_0(
// LLVM: call i32 @__obelisk_eval_checkpoint_body_v1_0({{.*}}) #[[NOINLINE:[0-9]+]]
// LLVM-LABEL: define {{.*}}@__obelisk_eval_path_dispatch_v1_0(
// LLVM: call i32 @guarded.__obelisk_eval_body_0({{.*}}) #[[NOINLINE]]
// LLVM: attributes #[[NOINLINE]] = { {{.*}}noinline{{.*}} }
