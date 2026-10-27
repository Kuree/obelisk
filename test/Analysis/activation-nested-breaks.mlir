// RUN: talos-opt %s '--test-talos-activation-regions=inputs=true' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// RUN: talos-opt %s '--test-talos-activation-regions=compare-input-dependencies=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=COMPARE

// The input-only dependency proof agrees with full publication analysis.
// COMPARE: inputs @break_join agree
// COMPARE: inputs @break_fill agree
// COMPARE: inputs @exit_initializer agree

// A break may bypass the normal loop exit and join the enclosing continuation
// (IEEE 1800-2023 12.8). Proving the recurrence does not certify an unbounded
// side path: the whole-activation CFG must still have no unproved cycle.
!array = !simulation.unpacked_array<0 : 3 x i8>
module {
  simulation.design @breaks {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : !array design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "break_join"
    // CHECK-LABEL: regions @break_join valid=1
    simulation.func @break_join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %flag = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %two = arith.constant 2 : i8
      cf.br ^outer(%zero : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %two : i8
      cf.cond_br %more, ^inner(%zero : i8), ^wait
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %two : i8
      cf.cond_br %again, ^body, ^normal_exit
    ^body:
      %stop = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      cf.cond_br %stop, ^join, ^latch
    ^latch:
      simulation.ref.store %k to %out : i8, !simulation.ref<i8>
      %nextk = arith.addi %k, %one : i8
      cf.br ^inner(%nextk : i8)
    ^normal_exit:
      simulation.ref.store %two to %out : i8, !simulation.ref<i8>
      cf.br ^join
    ^join:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %nexti = arith.addi %i, %one : i8
      cf.br ^outer(%nexti : i8)
    ^wait:
      simulation.suspend.change %flag to ^resume : !simulation.ref<i1>
    ^resume:
      cf.br ^outer(%zero : i8)
    }
    // The prefix and finishing sweep together cover every element, including
    // when the sweep has zero iterations. Landing blocks must retain the exact
    // initializer instead of merging the break iterations before the sweep.
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "break_fill"
    // CHECK-LABEL: regions @break_fill valid=1
    // CHECK: must-write s2[0,32)
    // CHECK-NEXT: may-write s2[0,32)
    simulation.func @break_fill(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %flag = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[2] : !simulation.ref<!array>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %four = arith.constant 4 : i8
      cf.br ^outer(%zero : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %four : i8
      cf.cond_br %more, ^body, ^wait
    ^body:
      %dest = simulation.ref.array_element %out[%i] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.ref.store %i to %dest : i8, !simulation.ref<i8>
      %stop = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      %nexti = arith.addi %i, %one : i8
      cf.cond_br %stop, ^finish(%nexti : i8), ^advance
    ^advance:
      simulation.ref.store %one to %dest : i8, !simulation.ref<i8>
      cf.br ^outer(%nexti : i8)
    ^finish(%j: i8):
      %again = arith.cmpi slt, %j, %four : i8
      cf.cond_br %again, ^fill, ^wait
    ^fill:
      %rest = simulation.ref.array_element %out[%j] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.ref.store %j to %rest : i8, !simulation.ref<i8>
      %nextj = arith.addi %j, %one : i8
      cf.br ^finish(%nextj : i8)
    ^wait:
      simulation.suspend.change %flag to ^resume : !simulation.ref<i1>
    ^resume:
      cf.br ^outer(%zero : i8)
    }
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "unbounded_exit"
    // CHECK-LABEL: regions @unbounded_exit valid=0 reason=termination
    simulation.func @unbounded_exit(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %flag = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %two = arith.constant 2 : i8
      cf.br ^outer(%zero : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %two : i8
      cf.cond_br %more, ^inner(%zero : i8), ^wait
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %two : i8
      cf.cond_br %again, ^body, ^normal_exit
    ^body:
      %stop = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      cf.cond_br %stop, ^forever, ^latch
    ^latch:
      simulation.ref.store %k to %out : i8, !simulation.ref<i8>
      %nextk = arith.addi %k, %one : i8
      cf.br ^inner(%nextk : i8)
    ^normal_exit:
      simulation.ref.store %two to %out : i8, !simulation.ref<i8>
      cf.br ^join
    ^join:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %nexti = arith.addi %i, %one : i8
      cf.br ^outer(%nexti : i8)
    ^forever:
      simulation.ref.store %one to %out : i8, !simulation.ref<i8>
      cf.br ^forever
    ^wait:
      simulation.suspend.change %flag to ^resume : !simulation.ref<i1>
    ^resume:
      cf.br ^outer(%zero : i8)
    }
    // The finishing sweep runs only on a break path, outside the outer
    // natural loop. Its initializer still has the outer true-edge range.
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "exit_initializer"
    // CHECK-LABEL: regions @exit_initializer valid=1
    simulation.func @exit_initializer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %flag = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %four = arith.constant 4 : i8
      cf.br ^outer(%zero : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %four : i8
      cf.cond_br %more, ^body, ^wait
    ^body:
      %stop = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      %nexti = arith.addi %i, %one : i8
      cf.cond_br %stop, ^finish(%nexti : i8), ^advance
    ^advance:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      cf.br ^outer(%nexti : i8)
    ^finish(%j: i8):
      %again = arith.cmpi slt, %j, %four : i8
      cf.cond_br %again, ^fill, ^wait
    ^fill:
      simulation.ref.store %j to %out : i8, !simulation.ref<i8>
      %nextj = arith.addi %j, %one : i8
      cf.br ^finish(%nextj : i8)
    ^wait:
      simulation.suspend.change %flag to ^resume : !simulation.ref<i1>
    ^resume:
      cf.br ^outer(%zero : i8)
    }
  }
}
