// RUN: obelisk-opt %s '--test-obelisk-activation-regions=slice-root=1' -o /dev/null 2>&1 | FileCheck %s
// RUN: obelisk-opt %s '--test-obelisk-activation-regions=compare-input-dependencies=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=COMPARE

// The input-only dependency proof agrees with full publication analysis.
// COMPARE: inputs @carried_result agree
// COMPARE: inputs @different_exit_values agree

// Finite does not mean dead. A carried SSA result and different values on
// early/normal exits must survive slicing, even with no stores in the body.
module {
  simulation.design @live_loops {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "carried_result"
    // CHECK-LABEL: regions @carried_result valid=1
    // CHECK-LABEL: activation slice @carried_result
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.load
    // CHECK: arith.xori
    // CHECK: cf.br
    // CHECK: simulation.ref.store
    simulation.func @carried_result(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %two = arith.constant 2 : i8
      %false = arith.constant false
      cf.br ^loop(%zero, %false : i8, i1)
    ^loop(%i: i8, %acc: i1):
      %more = arith.cmpi slt, %i, %two : i8
      cf.cond_br %more, ^body, ^done
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<i1> -> i1
      %combined = arith.xori %v, %acc : i1
      %next = arith.addi %i, %one : i8
      cf.br ^loop(%next, %combined : i8, i1)
    ^done:
      simulation.ref.store %acc to %output : i1, !simulation.ref<i1>
      simulation.suspend.change %input to ^resume : !simulation.ref<i1>
    ^resume:
      cf.br ^loop(%zero, %false : i8, i1)
    }
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "different_exit_values"
    // CHECK-LABEL: regions @different_exit_values valid=1
    // CHECK-LABEL: activation slice @different_exit_values
    // CHECK: arith.cmpi
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.load
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    simulation.func @different_exit_values(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %scratch = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %two = arith.constant 2 : i8
      %false = arith.constant false
      %true = arith.constant true
      cf.br ^loop(%zero : i8)
    ^loop(%i: i8):
      %more = arith.cmpi slt, %i, %two : i8
      cf.cond_br %more, ^body, ^done(%false : i1)
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<i1> -> i1
      cf.cond_br %v, ^done(%true : i1), ^advance
    ^advance:
      // Keep the source latch explicit through initial canonicalization.
      simulation.ref.store %false to %scratch : i1, !simulation.ref<i1>
      %next = arith.addi %i, %one : i8
      cf.br ^loop(%next : i8)
    ^done(%result: i1):
      simulation.ref.store %result to %output : i1, !simulation.ref<i1>
      simulation.suspend.change %input to ^resume : !simulation.ref<i1>
    ^resume:
      cf.br ^loop(%zero : i8)
    }
  }
}
