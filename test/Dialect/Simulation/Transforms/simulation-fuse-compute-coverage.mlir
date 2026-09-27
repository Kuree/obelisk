// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true}))' -o %t.plan
// RUN: obelisk-opt %t.plan --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// Coverage effects retain Tier-1 fusion. Entry hits execute only at bootstrap,
// wait hits on each rearm, body hits once per activation. Keepalives follow
// the fused symbol instead of referencing erased original owners.
// CHECK: obelisk_sim.coverage.keepalive @[[FUSED:__obelisk_fused_[0-9_]+]]
// CHECK: obelisk_sim.coverage.keepalive @[[FUSED]]
// CHECK: obelisk_sim.spawn @[[FUSED]]
// CHECK: obelisk_sim.func private @[[FUSED]](
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[0]
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[3]
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[1]
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[4]
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[2]
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[5]
// CHECK: obelisk_sim.func private @[[FUSED]].__obelisk_eval_body_{{[0-9]+}}(
// CHECK-NOT: obelisk_sim.coverage.point_hit {{.*}}[0]
// CHECK-NOT: obelisk_sim.coverage.point_hit {{.*}}[3]
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[2]
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[5]
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[1]
// CHECK: obelisk_sim.coverage.point_hit {{.*}}[4]
// CHECK: obelisk_sim.return

module attributes {obelisk.coverage.line_point_count = 6 : i64, schedule.native_scheduler = 3 : i32} {
  obelisk_sim.design @fusion {
    obelisk_sim.coverage.keepalive @a
    obelisk_sim.coverage.keepalive @b
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "fusion.a"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "fusion.b"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = obelisk_sim.context.storage %ctx[0] :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %a = obelisk_sim.spawn @a(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      %b = obelisk_sim.spawn @b(%ctx, %clock) :
        !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
        -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      %entryHit = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %entryHit[0] : !obelisk_sim.context
      cf.br ^wait
    ^wait:
      %waitHit = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %waitHit[1] : !obelisk_sim.context
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %bodyHit = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %bodyHit[2] : !obelisk_sim.context
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

    obelisk_sim.func private @b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %entryHit = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %entryHit[3] : !obelisk_sim.context
      cf.br ^wait
    ^wait:
      %waitHit = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %waitHit[4] : !obelisk_sim.context
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %bodyHit = arith.constant true
      obelisk_sim.coverage.point_hit %ctx if %bodyHit[5] : !obelisk_sim.context
      cf.br ^wait
    }
  }
}
