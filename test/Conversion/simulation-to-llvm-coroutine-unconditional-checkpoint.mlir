// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s
// RUN: sed 's/native_scheduler = 0/native_scheduler = 3/' %s \
// RUN:   | not obelisk-opt \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=EVAL-DIAG

// An owner whose activation reaches its checkpoint leaf unconditionally has
// no generated path to guard: the route probe can only ever answer
// "checkpoint".  Fracturing it into a path dispatcher reduces the whole
// activation to a bare checkpoint publication, dropping both the NBA staging
// that precedes the leaf and the edge qualification that selected the
// activation.  Such an owner is genuinely runtime-owned: `auto` must fall
// back, and an explicit `eval` request must be diagnosed rather than
// silently miscompiled.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 0 : i32
} {
  obelisk_sim.design @unconditional_checkpoint {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "handoff.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "handoff.clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "handoff.guarded"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %source = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %destination = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %clock_process = obelisk_sim.spawn @clock(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %guarded_process = obelisk_sim.spawn @guarded(
          %ctx, %clock, %source, %destination) :
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

    obelisk_sim.func @guarded(
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
      // The NBA publication and the display leaf share one block, so every
      // activation of this owner ends in Tier 3.
      %value = obelisk_sim.ref.load %source :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %value to %destination :
          (!obelisk_sim.logic<1>,
           !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %stdout = arith.constant -2147483647 : i32
      %message = obelisk_sim.bytes.constant "checkpoint"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
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
// CHECK-LABEL: llvm.func @guarded.__obelisk_coro_ramp
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// CHECK: llvm.call @obelisk_rt_v1_display

// EVAL-DIAG: an eval owner keeps an unguarded runtime leaf
