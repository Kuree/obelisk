// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true}))' -o %t.plan
// RUN: obelisk-opt %t.plan --pass-pipeline='builtin.module(simulation.design(obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// Coverage effects retain Tier-1 fusion. Entry hits execute only at bootstrap,
// wait hits on each rearm, body hits once per activation. Keepalives follow
// the fused symbol instead of referencing erased original owners.
// CHECK: simulation.coverage.keepalive @[[FUSED:__obelisk_fused_[0-9_]+]]
// CHECK: simulation.coverage.keepalive @[[FUSED]]
// CHECK: simulation.spawn @[[FUSED]]
// CHECK: simulation.func private @[[FUSED]](
// CHECK: simulation.coverage.point_hit {{.*}}[0]
// CHECK: simulation.coverage.point_hit {{.*}}[3]
// CHECK: simulation.coverage.point_hit {{.*}}[1]
// CHECK: simulation.coverage.point_hit {{.*}}[4]
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.coverage.point_hit {{.*}}[2]
// CHECK: simulation.coverage.point_hit {{.*}}[5]
// CHECK: simulation.func private @[[FUSED]].__obelisk_eval_body_{{[0-9]+}}(
// CHECK-NOT: simulation.coverage.point_hit {{.*}}[0]
// CHECK-NOT: simulation.coverage.point_hit {{.*}}[3]
// CHECK: simulation.coverage.point_hit {{.*}}[2]
// CHECK: simulation.coverage.point_hit {{.*}}[5]
// CHECK: simulation.coverage.point_hit {{.*}}[1]
// CHECK: simulation.coverage.point_hit {{.*}}[4]
// CHECK: simulation.return

module attributes {obelisk.coverage.line_point_count = 6 : i64, schedule.native_scheduler = 3 : i32} {
  simulation.design @fusion {
    simulation.coverage.keepalive @a
    simulation.coverage.keepalive @b
    simulation.code_unit.decl 1 in 0 always hierarchy "fusion.a"
    simulation.code_unit.decl 2 in 0 always hierarchy "fusion.b"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = simulation.context.storage %ctx[0] :
        !simulation.ref<!simulation.logic<1>>
      %a = simulation.spawn @a(%ctx, %clock) :
        !simulation.context, !simulation.ref<!simulation.logic<1>>
        -> !simulation.process
      %b = simulation.spawn @b(%ctx, %clock) :
        !simulation.context, !simulation.ref<!simulation.logic<1>>
        -> !simulation.process
      simulation.return
    }

    simulation.func private @a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      %entryHit = arith.constant true
      simulation.coverage.point_hit %ctx if %entryHit[0] : !simulation.context
      cf.br ^wait
    ^wait:
      %waitHit = arith.constant true
      simulation.coverage.point_hit %ctx if %waitHit[1] : !simulation.context
      simulation.suspend.edge posedge %clock to ^body :
        !simulation.ref<!simulation.logic<1>>
    ^body:
      %bodyHit = arith.constant true
      simulation.coverage.point_hit %ctx if %bodyHit[2] : !simulation.context
      %condition = arith.constant true
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      cf.cond_br %condition, ^left, ^right
    ^left:
      cf.br ^join(%one : i32)
    ^right:
      cf.br ^join(%two : i32)
    ^join(%value: i32):
      %sum = arith.addi %value, %one : i32
      cf.br ^wait
    }

    simulation.func private @b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %entryHit = arith.constant true
      simulation.coverage.point_hit %ctx if %entryHit[3] : !simulation.context
      cf.br ^wait
    ^wait:
      %waitHit = arith.constant true
      simulation.coverage.point_hit %ctx if %waitHit[4] : !simulation.context
      simulation.suspend.edge posedge %clock to ^body :
        !simulation.ref<!simulation.logic<1>>
    ^body:
      %bodyHit = arith.constant true
      simulation.coverage.point_hit %ctx if %bodyHit[5] : !simulation.context
      cf.br ^wait
    }
  }
}
