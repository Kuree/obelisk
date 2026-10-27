// RUN: talos-opt %s --pass-pipeline='builtin.module(test-talos-replicate,simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements,talos-sim-materialize-compute-fusion))' -o %t.fused
// RUN: FileCheck %s --check-prefix=FUSED --implicit-check-not='simulation.func @p' < %t.fused
// RUN: talos-opt %t.fused --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' -o /dev/null
// RUN: talos-opt %s --pass-pipeline='builtin.module(test-talos-replicate,simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements{max-members=64},talos-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=LIMIT --implicit-check-not=__talos_region_kernel_

// Sixty-six chained always_comb statements (p1 ... p65 expand from p#), with
// the first also closing the chain's tail into a result. A statement DAG has
// no per-member dirty mask and no 64-bit membership limit.
// FUSED-COUNT-1: simulation.spawn @__talos_region_kernel_
// FUSED: simulation.func private @__talos_region_kernel_
// FUSED-COUNT-67: simulation.ref.store
// FUSED: simulation.suspend.any %{{.*}} edges [0]
// LIMIT: simulation.func @p0
// LIMIT: simulation.func @p65

module {
  simulation.design @chain {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.storage.decl 8 in 0 : i8 design
    simulation.storage.decl 9 in 0 : i8 design
    simulation.storage.decl 10 in 0 : i8 design
    simulation.storage.decl 11 in 0 : i8 design
    simulation.storage.decl 12 in 0 : i8 design
    simulation.storage.decl 13 in 0 : i8 design
    simulation.storage.decl 14 in 0 : i8 design
    simulation.storage.decl 15 in 0 : i8 design
    simulation.storage.decl 16 in 0 : i8 design
    simulation.storage.decl 17 in 0 : i8 design
    simulation.storage.decl 18 in 0 : i8 design
    simulation.storage.decl 19 in 0 : i8 design
    simulation.storage.decl 20 in 0 : i8 design
    simulation.storage.decl 21 in 0 : i8 design
    simulation.storage.decl 22 in 0 : i8 design
    simulation.storage.decl 23 in 0 : i8 design
    simulation.storage.decl 24 in 0 : i8 design
    simulation.storage.decl 25 in 0 : i8 design
    simulation.storage.decl 26 in 0 : i8 design
    simulation.storage.decl 27 in 0 : i8 design
    simulation.storage.decl 28 in 0 : i8 design
    simulation.storage.decl 29 in 0 : i8 design
    simulation.storage.decl 30 in 0 : i8 design
    simulation.storage.decl 31 in 0 : i8 design
    simulation.storage.decl 32 in 0 : i8 design
    simulation.storage.decl 33 in 0 : i8 design
    simulation.storage.decl 34 in 0 : i8 design
    simulation.storage.decl 35 in 0 : i8 design
    simulation.storage.decl 36 in 0 : i8 design
    simulation.storage.decl 37 in 0 : i8 design
    simulation.storage.decl 38 in 0 : i8 design
    simulation.storage.decl 39 in 0 : i8 design
    simulation.storage.decl 40 in 0 : i8 design
    simulation.storage.decl 41 in 0 : i8 design
    simulation.storage.decl 42 in 0 : i8 design
    simulation.storage.decl 43 in 0 : i8 design
    simulation.storage.decl 44 in 0 : i8 design
    simulation.storage.decl 45 in 0 : i8 design
    simulation.storage.decl 46 in 0 : i8 design
    simulation.storage.decl 47 in 0 : i8 design
    simulation.storage.decl 48 in 0 : i8 design
    simulation.storage.decl 49 in 0 : i8 design
    simulation.storage.decl 50 in 0 : i8 design
    simulation.storage.decl 51 in 0 : i8 design
    simulation.storage.decl 52 in 0 : i8 design
    simulation.storage.decl 53 in 0 : i8 design
    simulation.storage.decl 54 in 0 : i8 design
    simulation.storage.decl 55 in 0 : i8 design
    simulation.storage.decl 56 in 0 : i8 design
    simulation.storage.decl 57 in 0 : i8 design
    simulation.storage.decl 58 in 0 : i8 design
    simulation.storage.decl 59 in 0 : i8 design
    simulation.storage.decl 60 in 0 : i8 design
    simulation.storage.decl 61 in 0 : i8 design
    simulation.storage.decl 62 in 0 : i8 design
    simulation.storage.decl 63 in 0 : i8 design
    simulation.storage.decl 64 in 0 : i8 design
    simulation.storage.decl 65 in 0 : i8 design
    simulation.storage.decl 66 in 0 : i8 design
    simulation.storage.decl 67 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "p0"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "p#" {test.replicate = 65 : i64, test.replicate_first = 1 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %p0 = simulation.spawn @p0(%ctx) : !simulation.context -> !simulation.process
      %p = simulation.spawn @"p#"(%ctx) {test.replicate = 65 : i64, test.replicate_first = 1 : i64} : !simulation.context -> !simulation.process
      simulation.return
    }
    // p0 also closes the chain's tail (storage 66) into the result (67).
    simulation.func @p0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %last = simulation.context.storage %ctx[66] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[67] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      %tail = simulation.ref.load %last : !simulation.ref<i8> -> i8
      simulation.ref.store %tail to %result : i8, !simulation.ref<i8>
      simulation.suspend.any %in, %last edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @"p#"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64, test.replicate = 65 : i64, test.replicate_first = 1 : i64, test.replicate_stride = {code_unit_id = 1 : i64}} {
      %in = simulation.context.storage %ctx[1] {test.replicate_stride = {id = 1 : i64}} : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[2] {test.replicate_stride = {id = 1 : i64}} : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
  }
}
