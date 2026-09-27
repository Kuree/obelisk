// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s
// RUN: sed 's/native_scheduler = 0/native_scheduler = 3/' %s \
// RUN:   | obelisk-opt \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=EVAL

// Read-only VPI retains safe-point visibility but cannot mutate state, so the
// generated plan remains the canonical plane and fixed net handles may read it
// directly. The owner in this fixture still misses the exact path-dispatch
// shape for an unrelated control-flow reason and keeps its canonical route.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 0 : i32
} {
  simulation.design @vpi_checkpoint_net {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "handoff.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "handoff.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "handoff.guarded"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design

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
      %net = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %guarded_process = simulation.spawn @guarded(
          %ctx, %clock, %net, %destination) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
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
        %source: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64},
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
      %value = simulation.net.read %source :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
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
      %stdout = arith.constant -2147483647 : i32
      %message = simulation.bytes.constant "checkpoint"
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      cf.br ^wait
    }
  }
}

// No path dispatcher or checkpoint callback is built for the declined owner.
// CHECK-NOT: llvm.func @__obelisk_eval_path_dispatch_v1_
// CHECK-NOT: llvm.func @__obelisk_eval_four_state_fallback_v1_
// CHECK-NOT: llvm.func @__obelisk_eval_checkpoint_body_v1_

// The activation keeps its canonical body, but its fixed net read addresses
// the generated canonical planes directly; no runtime state helper is in the
// hot activation. The display leaf stays inline.
// CHECK-LABEL: llvm.func @guarded.__obelisk_coro_ramp
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: llvm.mlir.addressof @__obelisk_state_value
// CHECK: llvm.load
// CHECK: llvm.mlir.addressof @__obelisk_state_unknown
// CHECK: llvm.load
// CHECK: llvm.call @obelisk_rt_v1_display

// Read-only VPI no longer rejects forced generated-eval lowering merely for a
// fixed canonical net read.
// EVAL: schedule.eval.generated
// EVAL-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
