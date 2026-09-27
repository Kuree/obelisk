// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments))' | FileCheck %s --check-prefix=FUSED
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=BLOCK-ARGS

// FUSED: simulation.design @fusion attributes {
// FUSED-SAME: schedule.static_fusion = [#schedule.fusion<id = 0, fragments = [{{[0-9]+}}, {{[0-9]+}}]>]
// BLOCK-ARGS: simulation.func private @__obelisk_fused_
// BLOCK-ARGS: ^bb{{[0-9]+}}(%[[VALUE:.*]]: i32):
// BLOCK-ARGS: arith.addi %[[VALUE]],
// BLOCK-ARGS-NOT: simulation.func private @a
// BLOCK-ARGS-NOT: simulation.func private @b

module {
  simulation.design @fusion {
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
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body :
        !simulation.ref<!simulation.logic<1>>
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

    simulation.func private @b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body :
        !simulation.ref<!simulation.logic<1>>
    ^body:
      cf.br ^wait
    }
  }
}
