// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o %t 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: FileCheck %s < %t
// Both operands are defined before the selected read. Reconstruct them together
// in the response slice so their publication does not create a false cycle
// through the transform. Preserve the selected snapshot before the later store
// (IEEE 1800-2023 4.6, 10.4.1).
// PLAN: 1 plans (1 data-dependence order)
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.func private @__obelisk_region_kernel_0_0
// CHECK: %[[INPUT_A:.*]] = simulation.ref.load
// CHECK: %[[INPUT_B:.*]] = simulation.ref.load
// CHECK: %[[PRIVATE_A:.*]] = simulation.ref.alloc
// CHECK: %[[PRIVATE_B:.*]] = simulation.ref.alloc
// CHECK: simulation.ref.store %[[INPUT_A]] to %[[PRIVATE_A]]
// CHECK: simulation.ref.store %[[INPUT_B]] to %[[PRIVATE_B]]
// CHECK: %[[SELECTED:.*]] = arith.select %{{.*}}, %[[PRIVATE_A]], %[[PRIVATE_B]]
// CHECK: %[[SNAPSHOT:.*]] = simulation.ref.load %[[SELECTED]]
// CHECK: simulation.ref.store %[[INPUT_B]] to %[[PRIVATE_A]]
// CHECK: %[[SUM:.*]] = arith.addi %[[SNAPSHOT]], %[[INPUT_B]]
// CHECK: simulation.ref.store %[[SUM]]
// CHECK: simulation.suspend.any
module {
  simulation.design @selected_temporaries {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %inA = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %inB = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %choose = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %a = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %b = simulation.context.storage %ctx[4] : !simulation.ref<i8>
      %response = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.ref.load %response : !simulation.ref<i8> -> i8
      %va = simulation.ref.load %inA : !simulation.ref<i8> -> i8
      %vb = simulation.ref.load %inB : !simulation.ref<i8> -> i8
      %c = simulation.ref.load %choose : !simulation.ref<i1> -> i1
      simulation.ref.store %va to %a : i8, !simulation.ref<i8>
      simulation.ref.store %vb to %b : i8, !simulation.ref<i8>
      cf.cond_br %c, ^left, ^right
    ^left:
      %leftValue = simulation.ref.load %a : !simulation.ref<i8> -> i8
      cf.br ^join(%leftValue : i8)
    ^right:
      %rightValue = simulation.ref.load %b : !simulation.ref<i8> -> i8
      cf.br ^join(%rightValue : i8)
    ^join(%snapshot: i8):
      simulation.ref.store %vb to %a : i8, !simulation.ref<i8>
      %sum = arith.addi %snapshot, %r : i8
      simulation.ref.store %sum to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %inA, %inB, %choose, %response edges [0, 0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i1>, !simulation.ref<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %a = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %response = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %a : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %response : i8, !simulation.ref<i8>
      simulation.suspend.change %a to ^body : !simulation.ref<i8>
    }
  }
}
