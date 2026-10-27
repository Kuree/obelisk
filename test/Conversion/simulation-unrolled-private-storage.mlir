// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-unroll-bounded-loops),talos-sim-sccp,simulation.func(canonicalize,cse),talos-sim-build-compute-graph,talos-sim-materialize-compute-fusion,schedule-localize-private-eval-storage))' | FileCheck %s --check-prefix=LOCAL
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-unroll-bounded-loops),talos-sim-sccp,simulation.func(canonicalize,cse),talos-sim-build-compute-graph,talos-sim-materialize-compute-fusion,schedule-localize-private-eval-storage,schedule-promote-eval-storage))' | FileCheck %s --check-prefix=SSA

// Static storage retains its value between assignments (LRM 6.8/6.21).
// This private array is written before each read in every activation, so its
// initial value is unobservable. Replication exposes constant source indices;
// the declared descending range becomes ordinal views, then scalar SSA.
!array = !simulation.unpacked_array<3 : 0 x i8>
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @private_array {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "private_array"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %out = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %observed = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      simulation.ref.store %observed to %input : i8, !simulation.ref<i8>
      %p = simulation.spawn @process(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %tmp = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %out = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %four = arith.constant 4 : i32
      %zero8 = arith.constant 0 : i8
      cf.br ^body
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      cf.br ^loop(%zero, %zero8 : i32, i8)
    ^loop(%i: i32, %sum: i8):
      %test = arith.cmpi ult, %i, %four : i32
      cf.cond_br %test, ^element, ^done(%sum : i8)
    ^element:
      %r = simulation.ref.array_element %tmp[%i] : (!simulation.ref<!array>, i32) -> !simulation.ref<i8>
      %ii = arith.trunci %i : i32 to i8
      %value = arith.addi %v, %ii : i8
      simulation.ref.store %value to %r : i8, !simulation.ref<i8>
      %loaded = simulation.ref.load %r : !simulation.ref<i8> -> i8
      %added = arith.addi %sum, %loaded : i8
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next, %added : i32, i8)
    ^done(%result: i8):
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
  }
}
// LOCAL-LABEL: simulation.func private @process.__talos_eval_body_
// LOCAL: %[[SLOT:.*]] = simulation.ref.alloc
// LOCAL-NOT: simulation.ref.array_element
// LOCAL: simulation.ref.subelement %[[SLOT]]{{\[\[3\]\]}}
// LOCAL: simulation.ref.subelement %[[SLOT]]{{\[\[2\]\]}}
// LOCAL: simulation.ref.subelement %[[SLOT]]{{\[\[1\]\]}}
// LOCAL: simulation.ref.subelement %[[SLOT]]{{\[\[0\]\]}}
// LOCAL: simulation.return
// SSA-LABEL: simulation.func private @process.__talos_eval_body_
// SSA-NOT: simulation.ref.alloc
// SSA-NOT: simulation.ref.array_element
// SSA-NOT: simulation.ref.subelement
// SSA: simulation.ref.store
// SSA-NOT: simulation.ref.store
// SSA: simulation.return
