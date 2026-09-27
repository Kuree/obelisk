// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// Standalone activation outlining must place the wait hit on rearm, not on
// entry: a terminating activation executes its body but never waits again.
// CHECK-LABEL: obelisk_sim.func private @actor.__obelisk_eval_body_0(
// CHECK-NOT: obelisk_sim.coverage.point_hit
// CHECK: cf.br
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[2]
// CHECK: cf.cond_br {{.*}}, ^[[REARM:bb[0-9]+]], ^[[EXIT:bb[0-9]+]]
// CHECK: ^[[EXIT]]:
// CHECK-NEXT: obelisk_sim.return
// CHECK: ^[[REARM]]:
// CHECK-NEXT: obelisk_sim.coverage.point_hit {{.*}}[1]
// CHECK-NEXT: obelisk_sim.return

module attributes {obelisk.coverage.line_point_count = 3 : i64, schedule.native_scheduler = 3 : i32} {
  obelisk_sim.design @rearm {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "rearm.actor"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : i1 design
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %condition = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i1>
      %process = obelisk_sim.spawn @actor(%ctx, %clock, %condition) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @actor(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %condition: !obelisk_sim.ref<i1>
          {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      %entryHit = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %entryHit[0] : !obelisk_sim.context
      cf.br ^wait
    ^wait:
      %waitHit = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %waitHit[1] : !obelisk_sim.context
      obelisk_sim.suspend.edge posedge %clock to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %bodyHit = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %bodyHit[2] : !obelisk_sim.context
      %continue = obelisk_sim.ref.load %condition : !obelisk_sim.ref<i1> -> i1
      cf.cond_br %continue, ^wait, ^exit
    ^exit:
      obelisk_sim.return
    }
  }
}
