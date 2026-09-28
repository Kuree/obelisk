// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s
// RUN: sed 's/native_scheduler = 0/native_scheduler = 3/' %s \
// RUN:   | obelisk-opt \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=EVAL

// An owner whose activation reaches its checkpoint leaf unconditionally has
// no generated path to guard: the route probe can only ever answer
// "checkpoint".  Fracturing it into a path dispatcher reduces the whole
// activation to a bare checkpoint publication, dropping both the NBA staging
// that precedes the leaf and the edge qualification that selected the
// activation. Such an owner is runtime-owned, but need not withdraw the
// whole clock evaluator: explicit eval checkpoints exactly this actor and
// resumes the generated clock plan after the runtime executes its full body.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 0 : i32
} {
  simulation.design @unconditional_checkpoint {
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
      // The NBA publication and the display leaf share one block, so every
      // activation of this owner ends in Tier 3.
      %value = simulation.ref.load %source :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.nba.enqueue %value to %destination :
          (!simulation.logic<1>,
           !simulation.ref<!simulation.logic<1>>) -> ()
      %stdout = arith.constant -2147483647 : i32
      %message = simulation.bytes.constant "checkpoint"
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      cf.br ^wait
    }
  }
}

// Falling back leaves no path dispatcher, no cold checkpoint callback, and no
// checkpoint publication behind.
// CHECK-NOT: llvm.func @__obelisk_eval_path_dispatch_v1_
// CHECK-NOT: llvm.func @__obelisk_eval_four_state_fallback_v1_
// CHECK-NOT: llvm.func @__obelisk_eval_checkpoint_body_v1_
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_queue_aot_checkpoint

// The activation keeps its complete body: the NBA publication that precedes
// the display leaf still stages into the accumulator and marks its dirty
// root, and the display stays inline rather than moving to a cold Tier-3
// callback.
// CHECK-LABEL: llvm.func @guarded.__obelisk_table_body
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// CHECK: llvm.call @obelisk_rt_v1_display

// EVAL-LABEL: llvm.func @__obelisk_direct_fragment_{{.*}}.__obelisk_execute.checkpoint(
// EVAL: llvm.call @obelisk_rt_v1_scheduler_execute_aot_actor
// EVAL: llvm.return
// EVAL-LABEL: llvm.func @__obelisk_direct_fragment_{{.*}}.__obelisk_execute(
// EVAL-NOT: llvm.call
// EVAL: llvm.mlir.addressof @__obelisk_eval_checkpoint_callback_v1
// EVAL: llvm.return
// EVAL-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// EVAL: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// EVAL: llvm.call @obelisk_rt_v1_scheduler_queue_aot_checkpoint
