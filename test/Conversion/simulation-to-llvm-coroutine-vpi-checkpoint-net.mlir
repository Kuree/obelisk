// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s
// RUN: sed 's/native_scheduler = 0/native_scheduler = 3/' %s \
// RUN:   | not obelisk-opt \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=EVAL-DIAG

// Requesting VPI observability withdraws the directly addressable handles for
// nets, so a net read materializes through a runtime plane accessor. A
// path-sensitive checkpoint probe that reads such a net would carry that call
// into the generated closure, which the closure verifier rejects. The owner
// keeps its canonical route instead, so observability costs performance
// rather than the build.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 0 : i32
} {
  obelisk_sim.design @vpi_checkpoint_net {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "handoff.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "handoff.clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "handoff.guarded"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design

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
      %net = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %guarded_process = obelisk_sim.spawn @guarded(
          %ctx, %clock, %net, %destination) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
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
        %source: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
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
      %value = obelisk_sim.net.read %source :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %known_path = obelisk_sim.logic.compare case_eq %value, %zero :
          (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      cf.cond_br %known_path, ^publish, ^checkpoint
    ^publish:
      obelisk_sim.nba.enqueue %value to %destination :
          (!obelisk_sim.logic<1>,
           !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      cf.br ^wait
    ^checkpoint:
      %unknown = obelisk_sim.logic.constant 0 : i1, 1 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %unknown to %destination :
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

// No path dispatcher or checkpoint callback is built for the declined owner.
// CHECK-NOT: llvm.func @__obelisk_eval_path_dispatch_v1_
// CHECK-NOT: llvm.func @__obelisk_eval_four_state_fallback_v1_
// CHECK-NOT: llvm.func @__obelisk_eval_checkpoint_body_v1_

// The activation keeps its canonical body: the net read goes through the
// runtime plane accessor and the display leaf stays inline.
// CHECK-LABEL: llvm.func @guarded.__obelisk_coro_ramp
// CHECK: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: llvm.call @obelisk_rt_v1_display

// EVAL-DIAG: an eval owner keeps an unguarded runtime leaf
