// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// IEEE 1800-2017 13.2: a task may contain time-controlling statements, and a
// function cannot enable a task. An `always` activation that calls a task
// therefore cannot be cloned into the zero-time eval body; it keeps its
// coroutine identity instead.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @eval_task_call {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "eval_task_call.driver"
    obelisk_sim.code_unit.decl 2 in 0 task hierarchy "eval_task_call.step"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clk = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @driver(%ctx, %clk) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @driver(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^activation
          {site = #obelisk_sim.continuation<id = 1>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^activation:
      obelisk_sim.task.call @step(%ctx) arguments 1 to ^resume
          {site = #obelisk_sim.continuation<id = 2>} : !obelisk_sim.context
    ^resume:
      cf.br ^wait
    }

    obelisk_sim.func private @step(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 2 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume
          {site = #obelisk_sim.continuation<id = 3>,
           timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^resume:
      obelisk_sim.return
    }
  }
}

// The activation keeps its task call, and no zero-time eval body is cloned
// out of it.
// CHECK-NOT: __obelisk_eval_body
// CHECK: obelisk_sim.func private @driver
// CHECK: obelisk_sim.task.call @step
// CHECK-NOT: __obelisk_eval_body
