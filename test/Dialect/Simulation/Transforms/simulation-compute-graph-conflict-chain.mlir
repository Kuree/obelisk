// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// Four mutually conflicting actors require an ordering, but the graph stores
// its transitive chain rather than all six pairs.
// CHECK: #schedule.edge<source = 0, target = 1, kind = conflict
// CHECK: #schedule.edge<source = 1, target = 2, kind = conflict
// CHECK: #schedule.edge<source = 2, target = 3, kind = conflict
// CHECK-NOT: kind = conflict

module {
  simulation.design @conflict_chain {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.a"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.b"
    simulation.code_unit.decl 3 in 0 initial hierarchy "top.c"
    simulation.code_unit.decl 4 in 0 initial hierarchy "top.d"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design

    simulation.func @a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %target = simulation.context.storage %ctx[0] :
        !simulation.ref<!simulation.logic<1>>
      %value = simulation.logic.constant true, false :
        !simulation.logic<1>
      simulation.ref.store %value to %target :
        !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    simulation.func @b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %target = simulation.context.storage %ctx[0] :
        !simulation.ref<!simulation.logic<1>>
      %value = simulation.logic.constant true, false :
        !simulation.logic<1>
      simulation.ref.store %value to %target :
        !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    simulation.func @c(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %target = simulation.context.storage %ctx[0] :
        !simulation.ref<!simulation.logic<1>>
      %value = simulation.logic.constant true, false :
        !simulation.logic<1>
      simulation.ref.store %value to %target :
        !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    simulation.func @d(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %target = simulation.context.storage %ctx[0] :
        !simulation.ref<!simulation.logic<1>>
      %value = simulation.logic.constant true, false :
        !simulation.logic<1>
      simulation.ref.store %value to %target :
        !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}
