// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// The first two owners form a cyclic cohort. Its repeated scratch publication
// pulls in observer, but producer_and_consumer -> bridge -> observer would
// become a new cycle around the contracted kernel unless bridge joins too.
// Absorbing bridge pulls in observer2 through another repeated publication;
// that contraction must in turn absorb bridge2. Both closures need a fixpoint.
// CHECK-LABEL: simulation.design @observer_closure
// CHECK: simulation.func @root
// CHECK-NOT: simulation.spawn
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.return
// CHECK: simulation.func private @__obelisk_region_kernel_0_0
module {
  simulation.design @observer_closure {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.storage.decl 8 in 0 : i8 design
    simulation.storage.decl 9 in 0 : i8 design
    simulation.storage.decl 10 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "bridge"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "observer"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "bridge2"
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "observer2"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %c = simulation.spawn @bridge(%ctx) : !simulation.context -> !simulation.process
      %d = simulation.spawn @observer(%ctx) : !simulation.context -> !simulation.process
      %e = simulation.spawn @bridge2(%ctx) : !simulation.context -> !simulation.process
      %f = simulation.spawn @observer2(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      simulation.ref.store %zero to %scratch : i8, !simulation.ref<i8>
      simulation.ref.store %a to %scratch : i8, !simulation.ref<i8>
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
    simulation.func @bridge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      %scratch = simulation.context.storage %ctx[8] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %output : i8, !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      simulation.ref.store %zero to %scratch : i8, !simulation.ref<i8>
      simulation.ref.store %v to %scratch : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i8>
    }
    simulation.func @observer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[7] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %w = simulation.ref.load %scratch : !simulation.ref<i8> -> i8
      %sum = arith.addi %v, %w : i8
      simulation.ref.store %sum to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %scratch edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @bridge2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %input = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[9] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i8>
    }
    simulation.func @observer2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      %input = simulation.context.storage %ctx[9] : !simulation.ref<i8>
      %scratch = simulation.context.storage %ctx[8] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[10] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %w = simulation.ref.load %scratch : !simulation.ref<i8> -> i8
      %sum = arith.addi %v, %w : i8
      simulation.ref.store %sum to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %scratch edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
  }
}
