// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// IEEE 1800-2017 10.6.2: the right-hand side of an intra-assignment event
// control is evaluated before suspension. The forwarded value is coroutine
// state and cannot be recomputed by a zero-time eval body after activation.

module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @suspend_forwarded_value {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always hierarchy "test.driver"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %dst = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<4>>
      %event = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<1>>
      %process = simulation.spawn @driver(%ctx, %dst, %event) :
          !simulation.context, !simulation.ref<!simulation.logic<4>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @driver(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %dst: !simulation.ref<!simulation.logic<4>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64},
        %event: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      %sampled = simulation.logic.constant 5 : i4, 0 : i4 :
          !simulation.logic<4>
      simulation.suspend.change %event to ^activation(
          %sampled : !simulation.logic<4>)
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<1>>
    ^activation(%value: !simulation.logic<4>):
      simulation.ref.store %value to %dst :
          !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      cf.br ^wait
    }
  }
}

// CHECK-NOT: __obelisk_eval_body
// CHECK-LABEL: simulation.func private @driver
// CHECK: simulation.suspend.change
// CHECK-SAME: to ^[[ACTIVATION:bb[0-9]+]]
// CHECK: ^[[ACTIVATION]](%[[VALUE:.*]]: !simulation.logic<4>):
// CHECK: simulation.ref.store %[[VALUE]]
// CHECK-NOT: __obelisk_eval_body
