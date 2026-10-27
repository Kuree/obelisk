// RUN: talos-opt %s --test-talos-unroll-loops --test-talos-storage-writes -o /dev/null 2>&1 | FileCheck %s

// Preserve path exclusivity, repeated execution, and conservative call effects
// with more than one word of tracked writes and long identity-transfer spans:
// each arm stores 65 times, with four additions between consecutive stores,
// once its loop is unrolled.
// CHECK-LABEL: function diamond
// CHECK-COUNT-130: once=1
// CHECK: pair 0,64 exclusive=0
// CHECK-NEXT: pair 0,65 exclusive=1
// CHECK: pair 64,65 exclusive=1
// CHECK: pair 65,129 exclusive=0
// CHECK-LABEL: function loop
// CHECK-COUNT-130: once=0
// CHECK: pair 0,64 exclusive=0
// CHECK-NEXT: pair 0,65 exclusive=1
// CHECK: pair 64,65 exclusive=1
// CHECK: pair 65,129 exclusive=0
// CHECK-LABEL: function call
// CHECK-COUNT-130: once=0
// CHECK: pair 0,65 exclusive=0
// CHECK: pair 64,65 exclusive=0
// CHECK: pair 65,129 exclusive=0

module {
  simulation.design @execution {
    func.func private @opaque()
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.code_unit.decl 1 in 0 function hierarchy "diamond"
    simulation.code_unit.decl 2 in 0 function hierarchy "loop"
    simulation.code_unit.decl 3 in 0 function hierarchy "call"
    simulation.func @diamond(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %input: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %c0 = arith.constant 0 : index
      %c1 = arith.constant 1 : index
      %c65 = arith.constant 65 : index
      cf.cond_br %condition, ^left, ^right
    ^left:
      %left_last = scf.for %i = %c0 to %c65 step %c1 iter_args(%value = %input) -> (i64) {
        %a = arith.addi %value, %input : i64
        %b = arith.addi %a, %input : i64
        %c = arith.addi %b, %input : i64
        %d = arith.addi %c, %input : i64
        simulation.ref.store %d to %root : i64, !simulation.ref<i64>
        scf.yield %d : i64
      }
      cf.br ^exit
    ^right:
      %right_last = scf.for %i = %c0 to %c65 step %c1 iter_args(%value = %input) -> (i64) {
        %a = arith.addi %value, %input : i64
        %b = arith.addi %a, %input : i64
        %c = arith.addi %b, %input : i64
        %d = arith.addi %c, %input : i64
        simulation.ref.store %d to %root : i64, !simulation.ref<i64>
        scf.yield %d : i64
      }
      cf.br ^exit
    ^exit:
      simulation.return
    }
    simulation.func @loop(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %input: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %c0 = arith.constant 0 : index
      %c1 = arith.constant 1 : index
      %c65 = arith.constant 65 : index
      cf.cond_br %condition, ^left, ^right
    ^left:
      %left_last = scf.for %i = %c0 to %c65 step %c1 iter_args(%value = %input) -> (i64) {
        %a = arith.addi %value, %input : i64
        %b = arith.addi %a, %input : i64
        %c = arith.addi %b, %input : i64
        %d = arith.addi %c, %input : i64
        simulation.ref.store %d to %root : i64, !simulation.ref<i64>
        scf.yield %d : i64
      }
      cf.br ^left
    ^right:
      %right_last = scf.for %i = %c0 to %c65 step %c1 iter_args(%value = %input) -> (i64) {
        %a = arith.addi %value, %input : i64
        %b = arith.addi %a, %input : i64
        %c = arith.addi %b, %input : i64
        %d = arith.addi %c, %input : i64
        simulation.ref.store %d to %root : i64, !simulation.ref<i64>
        scf.yield %d : i64
      }
      cf.br ^right
    ^exit:
      simulation.return
    }
    simulation.func @call(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %input: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %c0 = arith.constant 0 : index
      %c1 = arith.constant 1 : index
      %c65 = arith.constant 65 : index
      cf.cond_br %condition, ^left, ^right
    ^left:
      func.call @opaque() : () -> ()
      %left_last = scf.for %i = %c0 to %c65 step %c1 iter_args(%value = %input) -> (i64) {
        %a = arith.addi %value, %input : i64
        %b = arith.addi %a, %input : i64
        %c = arith.addi %b, %input : i64
        %d = arith.addi %c, %input : i64
        simulation.ref.store %d to %root : i64, !simulation.ref<i64>
        scf.yield %d : i64
      }
      cf.br ^exit
    ^right:
      func.call @opaque() : () -> ()
      %right_last = scf.for %i = %c0 to %c65 step %c1 iter_args(%value = %input) -> (i64) {
        %a = arith.addi %value, %input : i64
        %b = arith.addi %a, %input : i64
        %c = arith.addi %b, %input : i64
        %d = arith.addi %c, %input : i64
        simulation.ref.store %d to %root : i64, !simulation.ref<i64>
        scf.yield %d : i64
      }
      cf.br ^exit
    ^exit:
      simulation.return
    }
  }
}
