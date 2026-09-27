// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// A legal tie between conflicting processes must not create feedback. The
// producer has a later source ID, and is not a settling process. Choosing the
// opposite conflict edge creates a spurious two-member convergence group.
// CHECK-LABEL: simulation.design @initial_producer
// CHECK-SAME: #schedule.edge<source = 2, target = 1, kind = sensitivity
// CHECK-SAME: #schedule.edge<source = 2, target = 1, kind = conflict
// CHECK-SAME: #schedule.group<fragments = [2], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [1], schedule = acyclic
module {
  simulation.design @initial_producer {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "consumer"
    simulation.code_unit.decl 2 in 0 initial hierarchy "producer"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @a_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i8>
    }
    simulation.func @z_producer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %target = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = arith.constant 42 : i8
      simulation.ref.store %value to %target : i8, !simulation.ref<i8>
      simulation.return
    }
  }

  // Sensitivity targets the wait block; the consumer's work is its resumed
  // block. The conflict choice and region SCCs must use this same projection.
  // CHECK-LABEL: simulation.design @resumed_consumer
  // CHECK-SAME: #schedule.edge<source = 3, target = 1, kind = conflict
  // CHECK-SAME: #schedule.edge<source = 3, target = 2, kind = sensitivity
  // CHECK-SAME: #schedule.group<fragments = [3], schedule = acyclic
  // CHECK-SAME: #schedule.group<fragments = [1], schedule = acyclic
  // CHECK-SAME: #schedule.group<fragments = [2], schedule = acyclic
  simulation.design @resumed_consumer {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "consumer"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "producer"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.func @a_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^body : !simulation.ref<i8>
    }
    simulation.func @z_producer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      %target = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = arith.constant 42 : i8
      simulation.ref.store %value to %target : i8, !simulation.ref<i8>
      simulation.return
    }
  }

  // A real two-process feedback loop remains a convergence SCC.
  // CHECK-LABEL: simulation.design @feedback
  // CHECK-SAME: #schedule.group<fragments = [1, 3], schedule = convergence
  simulation.design @feedback {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "a"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "b"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.func @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %value = simulation.ref.load %input : !simulation.ref<i1> -> i1
      simulation.ref.store %value to %output : i1, !simulation.ref<i1>
      simulation.suspend.change %input to ^body : !simulation.ref<i1>
    }
    simulation.func @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %output = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %value = simulation.ref.load %input : !simulation.ref<i1> -> i1
      simulation.ref.store %value to %output : i1, !simulation.ref<i1>
      simulation.suspend.change %input to ^body : !simulation.ref<i1>
    }
  }

  // Event-region order already orders these accesses. A conflict chain must
  // not use a postponed fragment to serialize active-region work.
  // CHECK-LABEL: simulation.design @separate_regions
  // CHECK-NOT: kind = conflict
  // CHECK-SAME: #schedule.region<kind = active
  // CHECK-SAME: #schedule.region<kind = postponed
  // CHECK-NOT: kind = conflict
  simulation.design @separate_regions {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "active"
    simulation.code_unit.decl 2 in 0 final hierarchy "postponed"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.func @a_active(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %target = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = arith.constant 42 : i8
      simulation.ref.store %value to %target : i8, !simulation.ref<i8>
      simulation.return
    }
    simulation.func @b_postponed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 2 : i32, code_unit_id = 2 : i64, home_region = 16 : i32} {
      %target = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = simulation.ref.load %target : !simulation.ref<i8> -> i8
      simulation.return
    }
  }
}
