// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-suspension),talos-sim-build-compute-graph,schedule-plan-combinational-statements,talos-sim-materialize-compute-fusion))' -o %t
// RUN: FileCheck %s --implicit-check-not="simulation.spawn @producer_and_consumer" --implicit-check-not="simulation.spawn @transform" < %t
// RUN: talos-opt %t --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-suspension),talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' -o /dev/null

// The response is completely replaced through two integer packed-field writes.
// Its old value cannot create feedback or an unwatched activation input. Keep
// the request -> transform -> response schedule available with integer storage,
// just as it is with four-state logic.insert/logic.extract operations.
// CHECK: simulation.spawn @__talos_region_kernel_0_0
// CHECK: simulation.func private @__talos_region_kernel_0_0
module {
  simulation.design @slice_fusion_bits {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %old = simulation.ref.load %result : !simulation.ref<i8> -> i8
      %zero = arith.constant 0 : i64
      %four = arith.constant 4 : i64
      %shift = arith.constant 4 : i8
      %lo = arith.trunci %a : i8 to i4
      %shifted = arith.shrui %a, %shift : i8
      %hi = arith.trunci %shifted : i8 to i4
      %first = simulation.bits.dyn_insert %lo into %old at %zero : (i8, i4, i64) -> i8
      %last = simulation.bits.dyn_insert %hi into %first at %four : (i8, i4, i64) -> i8
      simulation.ref.store %last to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
  }
}
