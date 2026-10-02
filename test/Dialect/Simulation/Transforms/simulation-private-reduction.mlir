// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off},simulation.func(mem2reg)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=read},simulation.func(mem2reg)))' | FileCheck %s --check-prefix=READ
module {
  simulation.design @private_reduction {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 function hierarchy "reduction"
    simulation.code_unit.decl 2 in 0 function hierarchy "persistent"
    // A private reduction is promoted once, with generic mem2reg constructing
    // its loop-carried SSA value. No per-iteration canonical stores remain.
    // CHECK-LABEL: simulation.func @reduction
    // CHECK-NOT: simulation.ref.
    // CHECK: cf.br
    // CHECK-NOT: simulation.ref.
    // CHECK: arith.xori
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.return
    // READ-LABEL: simulation.func @reduction
    // READ: simulation.ref.store
    // READ: simulation.ref.store
    simulation.func @reduction(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: i32 {simulation.capture_kind = 2 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %six = arith.constant 6 : i32
      simulation.ref.store %zero to %root : i32, !simulation.ref<i32>
      cf.br ^header(%zero : i32)
    ^header(%i: i32):
      %continue = arith.cmpi slt, %i, %six : i32
      cf.cond_br %continue, ^body, ^exit
    ^body:
      %old = simulation.ref.load %root : !simulation.ref<i32> -> i32
      %next = arith.xori %old, %input : i32
      simulation.ref.store %next to %root : i32, !simulation.ref<i32>
      %increment = arith.addi %i, %one : i32
      cf.br ^header(%increment : i32)
    ^exit:
      %result = simulation.ref.load %root : !simulation.ref<i32> -> i32
      simulation.return %result : i32
    }
    // A reduction reading persistent contents keeps a canonical snapshot and
    // final store, even though its intermediate private updates use SSA.
    // CHECK-LABEL: simulation.func @persistent
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @persistent(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: i32 {simulation.capture_kind = 2 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %old = simulation.ref.load %root : !simulation.ref<i32> -> i32
      %a = arith.xori %old, %input : i32
      simulation.ref.store %a to %root : i32, !simulation.ref<i32>
      %b = arith.addi %a, %input : i32
      simulation.ref.store %b to %root : i32, !simulation.ref<i32>
      simulation.return %b : i32
    }
  }
}
