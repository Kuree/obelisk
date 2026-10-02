// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off}))' | FileCheck %s
!array = !simulation.unpacked_array<0 : 3 x i16>
module {
  simulation.design @induction {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "increasing"
    simulation.code_unit.decl 2 in 0 function hierarchy "decreasing"
    simulation.code_unit.decl 3 in 0 function hierarchy "modular"
    // The body selector is 0..3 without cloning any iteration. The final
    // header value is 4 and must not inherit the body's in-bounds certificate.
    // CHECK-LABEL: simulation.func @increasing
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.load
    // CHECK: arith.addi
    // CHECK: simulation.ref.store
    // CHECK: simulation.ref.load
    simulation.func @increasing(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i16 {simulation.capture_kind = 2 : i32}) -> i16 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %four = arith.constant 4 : i32
      cf.br ^header(%zero : i32)
    ^header(%i: i32):
      %continue = arith.cmpi slt, %i, %four : i32
      cf.cond_br %continue, ^body, ^exit
    ^body:
      %wide = arith.extsi %i : i32 to i65
      %cell = simulation.ref.array_element %root[%wide] : (!simulation.ref<!array>, i65) -> !simulation.ref<i16>
      simulation.ref.store %value to %cell : i16, !simulation.ref<i16>
      %after = simulation.ref.load %cell : !simulation.ref<i16> -> i16
      %next = arith.addi %i, %one : i32
      cf.br ^header(%next : i32)
    ^exit:
      %invalid = simulation.ref.array_element %root[%i] : (!simulation.ref<!array>, i32) -> !simulation.ref<i16>
      simulation.ref.store %value to %invalid : i16, !simulation.ref<i16>
      %default = simulation.ref.load %invalid : !simulation.ref<i16> -> i16
      simulation.return %default : i16
    }
    // CHECK-LABEL: simulation.func @decreasing
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.load
    // CHECK: arith.subi
    simulation.func @decreasing(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i16 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %three = arith.constant 3 : i32
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      cf.br ^header(%three : i32)
    ^header(%i: i32):
      %continue = arith.cmpi sge, %i, %zero : i32
      cf.cond_br %continue, ^body, ^exit
    ^body:
      %wide = arith.extsi %i : i32 to i65
      %cell = simulation.ref.array_element %root[%wide] : (!simulation.ref<!array>, i65) -> !simulation.ref<i16>
      simulation.ref.store %value to %cell : i16, !simulation.ref<i16>
      %after = simulation.ref.load %cell : !simulation.ref<i16> -> i16
      %next = arith.subi %i, %one : i32
      cf.br ^header(%next : i32)
    ^exit:
      simulation.return
    }
    // This finite modular loop crosses negative selectors. A termination
    // proof is insufficient for a bounds proof, even with a latch marker.
    // CHECK-LABEL: simulation.func @modular
    // CHECK: simulation.ref.store
    // CHECK: simulation.ref.load
    simulation.func @modular(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i16 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %initial = arith.constant -2 : i32
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      cf.br ^header(%initial : i32)
    ^header(%i: i32):
      %continue = arith.cmpi ne, %i, %zero : i32
      cf.cond_br %continue, ^body, ^exit
    ^body:
      %wide = arith.extsi %i : i32 to i65
      %cell = simulation.ref.array_element %root[%wide] : (!simulation.ref<!array>, i65) -> !simulation.ref<i16>
      simulation.ref.store %value to %cell : i16, !simulation.ref<i16>
      %after = simulation.ref.load %cell : !simulation.ref<i16> -> i16
      %next = arith.addi %i, %one : i32
      cf.br ^header(%next : i32) {schedule.bounded_loop_latch}
    ^exit:
      simulation.return
    }
  }
}
