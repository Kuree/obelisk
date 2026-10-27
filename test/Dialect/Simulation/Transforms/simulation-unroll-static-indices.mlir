// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-unroll-bounded-loops{maximum-loop-size=1000},canonicalize,cse)))' | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-unroll-bounded-loops{maximum-loop-size=2000},canonicalize,cse)))' | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-unroll-bounded-loops{maximum-loop-size=1},canonicalize,cse)))' | FileCheck %s --check-prefix=SMALL

// Replication must expose constant addresses before state-domain analysis,
// preserve every source-ordered publication, and repair loop-carried SSA.
// The nest size limit is independent of the termination proof.
!array = !simulation.unpacked_array<0 : 3 x i8>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @unroll_indices {
    simulation.scope.decl 0
    simulation.storage.decl 1 in 0 : i32 design
    simulation.code_unit.decl 4 in 0 root_initializer hierarchy "root"
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "indexed"
    simulation.code_unit.decl 2 in 0 function hierarchy "carried"
    simulation.code_unit.decl 3 in 0 function hierarchy "nested"

    // CHECK-LABEL: simulation.func @indexed
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.ref.subelement {{.*}}[0]
    // CHECK: simulation.ref.store
    // CHECK: simulation.ref.subelement {{.*}}[1]
    // CHECK: simulation.ref.store
    // CHECK: simulation.ref.subelement {{.*}}[2]
    // CHECK: simulation.ref.store
    // CHECK: simulation.ref.subelement {{.*}}[3]
    // CHECK: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    // SMALL-LABEL: simulation.func @indexed
    // SMALL: cf.cond_br
    // SMALL: simulation.ref.array_element
    simulation.func @indexed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 4 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %cell = simulation.ref.array_element %root[%i] : (!simulation.ref<!array>, i32) -> !simulation.ref<i8>
      %value = arith.trunci %i : i32 to i8
      simulation.ref.store %value to %cell : i8, !simulation.ref<i8>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @carried
    // CHECK: %[[SUM:.*]] = arith.constant 6 : i32
    // CHECK-NOT: cf.
    // CHECK: simulation.return %[[SUM]] : i32
    // SMALL-LABEL: simulation.func @carried
    // Even the smallest nonzero budget admits a fully constant reduction.
    // SMALL: %[[SUM:.*]] = arith.constant 6 : i32
    // SMALL-NOT: cf.
    // SMALL: simulation.return %[[SUM]] : i32
    simulation.func @carried(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero, %zero : i32, i32)
    ^head(%i: i32, %sum: i32):
      %limit = arith.constant 4 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit(%sum : i32)
    ^body:
      %value = arith.addi %sum, %i : i32
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next, %value : i32, i32)
    ^exit(%result: i32):
      simulation.return %result : i32
    }

    // Replicate the whole nest with the outer induction bound first.
    // CHECK-LABEL: simulation.func @nested
    // CHECK-COUNT-4: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    // SMALL-LABEL: simulation.func @nested
    // SMALL: cf.cond_br
    // SMALL: cf.cond_br
    simulation.func @nested(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %more = arith.cmpi ult, %i, %two : i32
      cf.cond_br %more, ^entry, ^exit
    ^entry:
      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %inner_more = arith.cmpi ult, %j, %two : i32
      cf.cond_br %inner_more, ^body, ^outer_latch
    ^body:
      %sum = arith.addi %i, %j : i32
      simulation.ref.store %sum to %out : i32, !simulation.ref<i32>
      %next_j = arith.addi %j, %one : i32
      cf.br ^inner(%next_j : i32)
    ^outer_latch:
      %next_i = arith.addi %i, %one : i32
      cf.br ^outer(%next_i : i32)
    ^exit:
      simulation.return
    }
    // The root calls each body, so every loop stays reachable from the design.
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 4 : i64} {
      simulation.call @indexed(%ctx) : (!simulation.context) -> ()
      %sum = simulation.call @carried(%ctx) : (!simulation.context) -> i32
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      simulation.call @nested(%ctx, %out) : (!simulation.context, !simulation.ref<i32>) -> ()
      %last = simulation.ref.load %out : !simulation.ref<i32> -> i32
      %array = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %a = simulation.ref.subelement %array[[0]] : !simulation.ref<!array> -> !simulation.ref<i8>
      %b = simulation.ref.subelement %array[[1]] : !simulation.ref<!array> -> !simulation.ref<i8>
      %c = simulation.ref.subelement %array[[2]] : !simulation.ref<!array> -> !simulation.ref<i8>
      %d = simulation.ref.subelement %array[[3]] : !simulation.ref<!array> -> !simulation.ref<i8>
      %av = simulation.ref.load %a : !simulation.ref<i8> -> i8
      %bv = simulation.ref.load %b : !simulation.ref<i8> -> i8
      %cv = simulation.ref.load %c : !simulation.ref<i8> -> i8
      %dv = simulation.ref.load %d : !simulation.ref<i8> -> i8
      %fmt = simulation.bytes.constant "unroll: %0d %0d %0d %0d sum=%0d last=%0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %av, %bv, %cv, %dv, %sum, %last) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0, 0] : !simulation.bytes, i8, i8, i8, i8, i32, i32
      simulation.return
    }
  }
}
