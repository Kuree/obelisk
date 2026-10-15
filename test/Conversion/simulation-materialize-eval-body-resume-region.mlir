// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// Generated evaluation runs a resumed activation in the Active region, or in
// Inactive for a deferred activation (IEEE 1800-2023 4.4.2.3). A clocking
// input sampler resumes in Observed (14.13), after Active-region updates of
// its source, so it keeps the generic scheduler.
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @regions {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always hierarchy "active"
    simulation.code_unit.decl 2 in 0 always hierarchy "inactive"
    simulation.code_unit.decl 3 in 0 always hierarchy "observed"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %in = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %a = simulation.spawn @active(%ctx, %clk, %in, %out) : !simulation.context, !simulation.ref<i1>, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %b = simulation.spawn @inactive(%ctx, %clk, %in, %out) : !simulation.context, !simulation.ref<i1>, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %c = simulation.spawn @observed(%ctx, %clk, %in, %out) : !simulation.context, !simulation.ref<i1>, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }

    // CHECK-LABEL: simulation.func @active(
    // CHECK-SAME: schedule.eval.body = @active.__obelisk_eval_body_
    simulation.func @active(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !simulation.ref<i1>
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      cf.br ^wait
    }

    // CHECK-LABEL: simulation.func @inactive(
    // CHECK-SAME: schedule.eval.body = @inactive.__obelisk_eval_body_
    simulation.func @inactive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {resume_region = 3 : i32} : !simulation.ref<i1>
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      cf.br ^wait
    }

    // CHECK-LABEL: simulation.func @observed(
    // CHECK-NOT: schedule.eval.body
    // CHECK: simulation.suspend.edge posedge
    simulation.func @observed(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {resume_region = 8 : i32} : !simulation.ref<i1>
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      cf.br ^wait
    }
  }
}
// CHECK-NOT: @observed.__obelisk_eval_body
