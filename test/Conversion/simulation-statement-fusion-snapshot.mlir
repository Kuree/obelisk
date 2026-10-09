// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// IEEE 1800-2023 4.3, 4.6(a), 10.4.1: splitting the request from the response
// must preserve the SSA snapshot of the first write, although the temporary
// later contains 2. Recomputing the response slice from that final value
// changes its output. Each slice must reconstruct the source-local versions.
// CHECK-LABEL: simulation.design @source_snapshot
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK-LABEL: simulation.func private @__obelisk_region_kernel_0_0
// CHECK-DAG: %[[ONE:.*]] = arith.constant 1 : i8
// CHECK-DAG: %[[TWO:.*]] = arith.constant 2 : i8
// CHECK: ^bb1:
// CHECK: %[[A:.*]] = simulation.ref.load
// CHECK: %[[R:.*]] = arith.addi %[[A]], %[[ONE]]
// CHECK: %[[OUT:.*]] = arith.addi %[[A]], %[[TWO]]
// CHECK: simulation.ref.store %[[OUT]]
// CHECK: simulation.suspend.any
module {
  simulation.design @source_snapshot {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
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
      %temp = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %one = arith.constant 1 : i8
      %two = arith.constant 2 : i8
      simulation.ref.store %one to %temp : i8, !simulation.ref<i8>
      %snapshot = simulation.ref.load %temp : !simulation.ref<i8> -> i8
      simulation.ref.store %two to %temp : i8, !simulation.ref<i8>
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      %sum = arith.addi %r, %snapshot : i8
      simulation.ref.store %sum to %result : i8, !simulation.ref<i8>
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
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
  }
}
