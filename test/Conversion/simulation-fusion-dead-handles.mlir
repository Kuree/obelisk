// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true},talos-sim-materialize-compute-fusion,talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' -o %t 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: FileCheck %s < %t
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-sccp,simulation.func(canonicalize,cse),talos-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true}))' -o /dev/null 2>&1 | FileCheck %s --check-prefix=PLAN

// A reference in an unreachable arm has no handle lattice. It cannot make
// the owner opaque and thereby destroy dataflow scheduling for the cohort.
// Exercise both defensive admission and structural SCCP cleanup.
// PLAN: 1 plans (1 data-dependence order)
// CHECK: simulation.spawn @__talos_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.func private @__talos_region_kernel_0_0
// CHECK-NOT: simulation.ref.subelement
// CHECK: simulation.suspend.any
module {
  simulation.design @dead_handle_guard {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : !simulation.unpacked_array<0 : 1 x i8> design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always hierarchy "sample"
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
      %array = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %zero = arith.constant 0 : i32
      cf.br ^body
    ^body:
      %false = arith.cmpi ne, %zero, %zero : i32
      cf.cond_br %false, ^dead, ^compute
    ^dead:
      %element = simulation.ref.subelement %array[[1]] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>> -> !simulation.ref<i8>
      %unreachable = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %unreachable to %result : i8, !simulation.ref<i8>
      cf.br ^compute
    ^compute:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
  }
}
