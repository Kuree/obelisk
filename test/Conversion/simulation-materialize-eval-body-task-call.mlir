// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// IEEE 1800-2017 13.2: a task may contain time-controlling statements, and a
// function cannot enable a task. An `always` activation that calls a task
// therefore cannot be cloned into the zero-time eval body; it keeps its
// coroutine identity instead.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @eval_task_call {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always hierarchy "eval_task_call.driver"
    simulation.code_unit.decl 2 in 0 task hierarchy "eval_task_call.step"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clk = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %process = simulation.spawn @driver(%ctx, %clk) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      simulation.return
    }

    simulation.func private @driver(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^activation
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<1>>
    ^activation:
      simulation.task.call @step(%ctx) arguments 1 to ^resume
          {site = #schedule.continuation<id = 2>} : !simulation.context
    ^resume:
      cf.br ^wait
    }

    simulation.func private @step(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
          {site = #schedule.continuation<id = 3>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^resume:
      simulation.return
    }
  }
}

// The activation keeps its task call, and no zero-time eval body is cloned
// out of it.
// CHECK-NOT: __obelisk_eval_body
// CHECK: simulation.func private @driver
// CHECK: simulation.task.call @step
// CHECK-NOT: __obelisk_eval_body
