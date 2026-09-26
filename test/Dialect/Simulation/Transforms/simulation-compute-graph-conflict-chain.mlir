// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// Four mutually conflicting actors require an ordering, but the graph stores
// its transitive chain rather than all six pairs.
// CHECK: #obelisk_sim.edge<source = 0, target = 1, kind = conflict
// CHECK: #obelisk_sim.edge<source = 1, target = 2, kind = conflict
// CHECK: #obelisk_sim.edge<source = 2, target = 3, kind = conflict
// CHECK-NOT: kind = conflict

module {
  obelisk_sim.design @conflict_chain {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.a"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.b"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "top.c"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "top.d"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %target = obelisk_sim.context.storage %ctx[0] :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant true, false :
        !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %target :
        !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    obelisk_sim.func @b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %target = obelisk_sim.context.storage %ctx[0] :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant true, false :
        !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %target :
        !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    obelisk_sim.func @c(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %target = obelisk_sim.context.storage %ctx[0] :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant true, false :
        !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %target :
        !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    obelisk_sim.func @d(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %target = obelisk_sim.context.storage %ctx[0] :
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant true, false :
        !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %target :
        !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }
}
