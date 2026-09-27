// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=WAIT-VIEW

// WAIT-VIEW: simulation.design @wait_view
// WAIT-VIEW-NOT: __obelisk_fused_

module {
  simulation.design @wait_view {
    simulation.code_unit.decl 1 in 0 always hierarchy "wait_view.a"
    simulation.code_unit.decl 2 in 0 always hierarchy "wait_view.b"
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
      %view = simulation.ref.extract %clock from 0 :
        !simulation.ref<!simulation.logic<1>>
        -> !simulation.ref<!simulation.logic<1>>
      simulation.suspend.edge posedge %view to ^body :
        !simulation.ref<!simulation.logic<1>>
    ^body:
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
