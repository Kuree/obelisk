// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// A legal tie between conflicting processes must not create feedback. The
// producer has a later source ID, and is not a settling process. Choosing the
// opposite conflict edge creates a spurious two-member convergence group.
// CHECK-LABEL: obelisk_sim.design @initial_producer
// CHECK-SAME: #obelisk_sim.edge<source = 2, target = 1, kind = sensitivity
// CHECK-SAME: #obelisk_sim.edge<source = 2, target = 1, kind = conflict
// CHECK-SAME: #obelisk_sim.group<fragments = [2], schedule = acyclic
// CHECK-SAME: #obelisk_sim.group<fragments = [1], schedule = acyclic
module {
  obelisk_sim.design @initial_producer {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 always_comb hierarchy "consumer"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "producer"
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.storage.decl 1 in 0 : i8 design
    obelisk_sim.func @a_consumer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %output = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i8>
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.ref.store %value to %output : i8, !obelisk_sim.ref<i8>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<i8>
    }
    obelisk_sim.func @z_producer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %target = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 42 : i8
      obelisk_sim.ref.store %value to %target : i8, !obelisk_sim.ref<i8>
      obelisk_sim.return
    }
  }

  // Sensitivity targets the wait block; the consumer's work is its resumed
  // block. The conflict choice and region SCCs must use this same projection.
  // CHECK-LABEL: obelisk_sim.design @resumed_consumer
  // CHECK-SAME: #obelisk_sim.edge<source = 3, target = 1, kind = conflict
  // CHECK-SAME: #obelisk_sim.edge<source = 3, target = 2, kind = sensitivity
  // CHECK-SAME: #obelisk_sim.group<fragments = [3], schedule = acyclic
  // CHECK-SAME: #obelisk_sim.group<fragments = [1], schedule = acyclic
  // CHECK-SAME: #obelisk_sim.group<fragments = [2], schedule = acyclic
  obelisk_sim.design @resumed_consumer {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 always_comb hierarchy "consumer"
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "producer"
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.storage.decl 1 in 0 : i8 design
    obelisk_sim.func @a_consumer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %output = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i8>
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.ref.store %value to %output : i8, !obelisk_sim.ref<i8>
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<i8>
    }
    obelisk_sim.func @z_producer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      %target = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 42 : i8
      obelisk_sim.ref.store %value to %target : i8, !obelisk_sim.ref<i8>
      obelisk_sim.return
    }
  }

  // A real two-process feedback loop remains a convergence SCC.
  // CHECK-LABEL: obelisk_sim.design @feedback
  // CHECK-SAME: #obelisk_sim.group<fragments = [1, 3], schedule = convergence
  obelisk_sim.design @feedback {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "a"
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "b"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i1 design
    obelisk_sim.func @a(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %output = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i1>
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.ref.store %value to %output : i1, !obelisk_sim.ref<i1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<i1>
    }
    obelisk_sim.func @b(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i1>
      %output = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.ref.store %value to %output : i1, !obelisk_sim.ref<i1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<i1>
    }
  }

  // Event-region order already orders these accesses. A conflict chain must
  // not use a postponed fragment to serialize active-region work.
  // CHECK-LABEL: obelisk_sim.design @separate_regions
  // CHECK-NOT: kind = conflict
  // CHECK-SAME: #obelisk_sim.region<kind = active
  // CHECK-SAME: #obelisk_sim.region<kind = postponed
  // CHECK-NOT: kind = conflict
  obelisk_sim.design @separate_regions {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "active"
    obelisk_sim.code_unit.decl 2 in 0 final hierarchy "postponed"
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @a_active(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %target = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 42 : i8
      obelisk_sim.ref.store %value to %target : i8, !obelisk_sim.ref<i8>
      obelisk_sim.return
    }
    obelisk_sim.func @b_postponed(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 2 : i32, code_unit_id = 2 : i64, home_region = 16 : i32} {
      %target = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = obelisk_sim.ref.load %target : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return
    }
  }
}
