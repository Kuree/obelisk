// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments))' | FileCheck %s --check-prefix=FUSED
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=BLOCK-ARGS

// FUSED: obelisk_sim.design @fusion attributes {
// FUSED-SAME: schedule.static_fusion = [#schedule.fusion<id = 0, fragments = [{{[0-9]+}}, {{[0-9]+}}]>]
// BLOCK-ARGS: obelisk_sim.func private @__obelisk_fused_
// BLOCK-ARGS: ^bb{{[0-9]+}}(%[[VALUE:.*]]: i32):
// BLOCK-ARGS: arith.addi %[[VALUE]],
// BLOCK-ARGS-NOT: obelisk_sim.func private @a
// BLOCK-ARGS-NOT: obelisk_sim.func private @b

module {
  obelisk_sim.design @fusion {
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
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
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
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^body :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      cf.br ^wait
    }
  }
}
