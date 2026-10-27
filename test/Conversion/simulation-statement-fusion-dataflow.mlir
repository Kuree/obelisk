// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements,talos-sim-materialize-compute-fusion,talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' | FileCheck %s
// RUN: talos-opt %s '--test-talos-set-module-attributes=attrs="{schedule.native.max_inline_ops = 1 : i64}"' > %t.low-inline.mlir
// RUN: talos-opt %t.low-inline.mlir --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements,talos-sim-materialize-compute-fusion,talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' | FileCheck %s
// RUN: talos-opt %s '--test-talos-set-module-attributes=attrs="{schedule.native.max_statement_ops = 1 : i64}"' > %t.low-statement.mlir
// RUN: talos-opt %t.low-statement.mlir --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true}))' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BUDGET
// BUDGET: statement component rejected: operation budget

// Statement order is cyclic: producer_and_consumer reads bank_result before it
// writes bank_address, and transform computes bank_result from bank_address.
// Each store's data is acyclic, and nothing outside the cohort waits on any
// written range, so the cohort runs once in data-dependence order. This is
// not a reference-algorithm execution (IEEE 1800-2023 4.6(a)); 4.3 admits it
// because no user-visible observer can see the transitions it removes.
// CHECK-LABEL: simulation.design @source_order_cycle
// CHECK: simulation.spawn @__talos_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK-LABEL: simulation.func private @__talos_region_kernel_0_0
// CHECK: %[[ADDRESS:.*]] = simulation.context.storage %{{.*}}[0]
// CHECK: ^bb1:
// CHECK: %[[A:.*]] = simulation.ref.load %[[ADDRESS]]
// CHECK: simulation.ref.store %[[A]] to
// CHECK: %[[R:.*]] = arith.addi %[[A]]
// CHECK: simulation.ref.store %[[R]] to
// CHECK: simulation.ref.store %[[R]] to
// CHECK: simulation.suspend.any %[[ADDRESS]] edges [0] to ^bb1
module {
  simulation.design @source_order_cycle {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
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
