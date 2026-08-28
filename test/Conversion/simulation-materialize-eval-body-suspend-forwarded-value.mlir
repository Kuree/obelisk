// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// IEEE 1800-2017 10.6.2: the right-hand side of an intra-assignment event
// control is evaluated before suspension. The forwarded value is coroutine
// state and cannot be recomputed by a zero-time eval body after activation.

module attributes {obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @suspend_forwarded_value {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "test.driver"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %dst = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %event = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @driver(%ctx, %dst, %event) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<4>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @driver(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %dst: !obelisk_sim.ref<!obelisk_sim.logic<4>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64},
        %event: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      %sampled = obelisk_sim.logic.constant 5 : i4, 0 : i4 :
          !obelisk_sim.logic<4>
      obelisk_sim.suspend.change %event to ^activation(
          %sampled : !obelisk_sim.logic<4>)
          {site = #obelisk_sim.continuation<id = 1>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^activation(%value: !obelisk_sim.logic<4>):
      obelisk_sim.ref.store %value to %dst :
          !obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>
      cf.br ^wait
    }
  }
}

// CHECK-NOT: __obelisk_eval_body
// CHECK-LABEL: obelisk_sim.func private @driver
// CHECK: obelisk_sim.suspend.change
// CHECK-SAME: to ^[[ACTIVATION:bb[0-9]+]]
// CHECK: ^[[ACTIVATION]](%[[VALUE:.*]]: !obelisk_sim.logic<4>):
// CHECK: obelisk_sim.ref.store %[[VALUE]]
// CHECK-NOT: __obelisk_eval_body
