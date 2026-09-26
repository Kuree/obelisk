// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-mark-bounded-loops)))' | FileCheck %s

// Canonicalization folds the empty join of `if (en) q <= 0;` into the header
// of a following `for` loop, which then has two entry edges. Both start the
// induction at the same constant, so they are one entry and the loop keeps
// its termination proof. Entries that start at different constants do not
// describe one induction sequence and are left unmarked.
module {
  obelisk_sim.design @loops {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "merged"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "different"

    // CHECK-LABEL: obelisk_sim.func @merged
    // CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}} {obelisk_sim.bounded_loop_header}
    // CHECK: cf.br ^{{.*}}(%{{.*}} : i32) {obelisk_sim.bounded_loop_latch}
    obelisk_sim.func @merged(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %flag: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 1 : i32}, %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %en = obelisk_sim.ref.load %flag : !obelisk_sim.ref<i1> -> i1
      cf.cond_br %en, ^then, ^head(%zero : i32)
    ^then:
      %again = arith.constant 0 : i32
      cf.br ^head(%again : i32)
    ^head(%i: i32):
      %limit = arith.constant 81 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @different
    // CHECK-NOT: bounded_loop
    // CHECK: obelisk_sim.return
    obelisk_sim.func @different(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %flag: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 1 : i32}, %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      %en = obelisk_sim.ref.load %flag : !obelisk_sim.ref<i1> -> i1
      cf.cond_br %en, ^then, ^head(%zero : i32)
    ^then:
      %five = arith.constant 5 : i32
      cf.br ^head(%five : i32)
    ^head(%i: i32):
      %limit = arith.constant 81 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      obelisk_sim.return
    }
  }
}
