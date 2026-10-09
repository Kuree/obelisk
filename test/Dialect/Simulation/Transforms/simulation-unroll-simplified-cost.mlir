// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-loop-size=8},canonicalize,cse)))' | FileCheck %s

// Equal trip counts and similar source bodies have different residual costs.
// Admission must account for the IR that constant propagation, dead guards,
// static index folding, and memory promotion actually remove. A raw expanded
// operation-count cutoff of eight would reject every positive case here.
module {
  simulation.design @simplified_cost {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "constant_reduction"
    simulation.code_unit.decl 2 in 0 function hierarchy "every_store_observable"
    simulation.code_unit.decl 3 in 0 function hierarchy "constant_guard"
    simulation.code_unit.decl 4 in 0 function hierarchy "local_array"
    simulation.code_unit.decl 5 in 0 function hierarchy "unrelated_dead_code"
    simulation.code_unit.decl 6 in 0 function hierarchy "construction_limit"

    // The 64-iteration reduction collapses to its exact fixed-width result.
    // CHECK-LABEL: simulation.func @constant_reduction(
    // CHECK: %[[TOTAL:.*]] = arith.constant 2016 : i32
    // CHECK-NOT: cf.
    // CHECK: simulation.return %[[TOTAL]] : i32
    simulation.func @constant_reduction(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 64 : i32
      cf.br ^loop(%zero, %zero : i32, i32)
    ^loop(%i: i32, %sum: i32):
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^done(%sum : i32)
    ^body:
      %added = arith.addi %sum, %i : i32
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next, %added : i32, i32)
    ^done(%sum_out: i32):
      simulation.return %sum_out : i32
    }

    // Every publication survives. The same 64 iterations exceed the reduced
    // cost limit, and trial simplification must leave the original intact.
    // CHECK-LABEL: simulation.func @every_store_observable(
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @every_store_observable(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 64 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^done
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^done:
      simulation.return
    }

    // Only the selected iteration publishes; both loop and inner guards fold.
    // CHECK-LABEL: simulation.func @constant_guard(
    // CHECK: %[[SEVEN:.*]] = arith.constant 7 : i32
    // CHECK-NOT: cf.
    // CHECK: simulation.ref.store %[[SEVEN]]
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @constant_guard(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %seven = arith.constant 7 : i32
      %limit = arith.constant 64 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^test, ^done
    ^test:
      %selected = arith.cmpi eq, %i, %seven : i32
      cf.cond_br %selected, ^write, ^latch
    ^write:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      cf.br ^latch
    ^latch:
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^done:
      simulation.return
    }

    // Indexed private memory becomes scalar SSA, then a constant. The cost
    // model must include this benefit before deciding whether to replicate.
    // CHECK-LABEL: simulation.func @local_array(
    // CHECK: %[[LAST:.*]] = arith.constant 15 : i8
    // CHECK-NOT: simulation.ref.
    // CHECK-NOT: cf.
    // CHECK: simulation.return %[[LAST]] : i8
    simulation.func @local_array(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %initial = simulation.aggregate.default : !simulation.unpacked_array<0 : 15 x i8>
      %array = simulation.ref.alloc %initial : !simulation.unpacked_array<0 : 15 x i8> -> !simulation.ref<!simulation.unpacked_array<0 : 15 x i8>>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 16 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^done
    ^body:
      %ref = simulation.ref.array_element %array[%i] : (!simulation.ref<!simulation.unpacked_array<0 : 15 x i8>>, i32) -> !simulation.ref<i8>
      %v = arith.trunci %i : i32 to i8
      simulation.ref.store %v to %ref : i8, !simulation.ref<i8>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^done:
      %last = simulation.ref.subelement %array[[15]] : !simulation.ref<!simulation.unpacked_array<0 : 15 x i8>> -> !simulation.ref<i8>
      %value = simulation.ref.load %last : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }
    // Pre-existing dead stores cannot pay for expanding an unrelated loop.
    // Normalize the original and candidate before measuring their difference.
    // CHECK-LABEL: simulation.func @unrelated_dead_code(
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @unrelated_dead_code(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %never = arith.constant false
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %eight = arith.constant 8 : i32
      cf.cond_br %never, ^dead, ^loop(%zero : i32)
    ^dead:
      simulation.ref.store %zero to %out : i32, !simulation.ref<i32>
      simulation.ref.store %zero to %out : i32, !simulation.ref<i32>
      simulation.ref.store %zero to %out : i32, !simulation.ref<i32>
      simulation.ref.store %zero to %out : i32, !simulation.ref<i32>
      simulation.ref.store %zero to %out : i32, !simulation.ref<i32>
      simulation.ref.store %zero to %out : i32, !simulation.ref<i32>
      simulation.ref.store %zero to %out : i32, !simulation.ref<i32>
      simulation.ref.store %zero to %out : i32, !simulation.ref<i32>
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi ult, %i, %eight : i32
      cf.cond_br %more, ^body, ^done
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^done:
      simulation.return
    }

    // Analysis work remains bounded even when all 4096 iterations would fold.
    // Crossing the construction ceiling refuses the entire nest, without
    // accidentally replicating just the inner loop.
    // CHECK-LABEL: simulation.func @construction_limit(
    // CHECK: cf.cond_br
    // CHECK: cf.cond_br
    // CHECK: arith.addi
    // CHECK: simulation.return
    simulation.func @construction_limit(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 64 : i32
      cf.br ^outer(%zero, %zero : i32, i32)
    ^outer(%i: i32, %sum: i32):
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^inner(%zero, %sum : i32, i32), ^done(%sum : i32)
    ^inner(%j: i32, %subtotal: i32):
      %inside = arith.cmpi ult, %j, %limit : i32
      cf.cond_br %inside, ^body, ^latch
    ^body:
      %add = arith.addi %subtotal, %j : i32
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj, %add : i32, i32)
    ^latch:
      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti, %subtotal : i32, i32)
    ^done(%result: i32):
      simulation.return %result : i32
    }
  }
}
