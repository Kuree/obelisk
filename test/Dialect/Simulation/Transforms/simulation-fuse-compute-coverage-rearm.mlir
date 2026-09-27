// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// Standalone activation outlining must place the wait hit on rearm, not on
// entry: a terminating activation executes its body but never waits again.
// CHECK-LABEL: simulation.func private @actor.__obelisk_eval_body_0(
// CHECK-NOT: simulation.coverage.point_hit
// CHECK: cf.br
// CHECK: simulation.coverage.point_hit {{.*}}[2]
// CHECK: cf.cond_br {{.*}}, ^[[REARM:bb[0-9]+]], ^[[EXIT:bb[0-9]+]]
// CHECK: ^[[EXIT]]:
// CHECK-NEXT: simulation.return
// CHECK: ^[[REARM]]:
// CHECK-NEXT: simulation.coverage.point_hit {{.*}}[1]
// CHECK-NEXT: simulation.return

module attributes {obelisk.coverage.line_point_count = 3 : i64, schedule.native_scheduler = 3 : i32} {
  simulation.design @rearm {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always hierarchy "rearm.actor"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %condition = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %process = simulation.spawn @actor(%ctx, %clock, %condition) :
        !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func private @actor(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %condition: !simulation.ref<i1>
          {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      %entryHit = arith.constant true
      simulation.coverage.point_hit %ctx if %entryHit[0] : !simulation.context
      cf.br ^wait
    ^wait:
      %waitHit = arith.constant true
      simulation.coverage.point_hit %ctx if %waitHit[1] : !simulation.context
      simulation.suspend.edge posedge %clock to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %bodyHit = arith.constant true
      simulation.coverage.point_hit %ctx if %bodyHit[2] : !simulation.context
      %continue = simulation.ref.load %condition : !simulation.ref<i1> -> i1
      cf.cond_br %continue, ^wait, ^exit
    ^exit:
      simulation.return
    }
  }
}
