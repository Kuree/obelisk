// RUN: talos-opt %s '--test-talos-activation-regions=reaching-values=true inputs=true' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// RUN: talos-opt %s '--test-talos-activation-regions=compare-input-dependencies=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=COMPARE

// The input-only dependency proof agrees with full publication analysis.
// COMPARE: inputs @whole_copy agree

// An array copy in each iteration publishes the next element's old value
// until that element is assigned. Only the final reaching versions are free
// of the incoming scratch state. These facts alone do not authorize hiding
// the intermediate updates (IEEE 1800-2023 4.3, 4.6(a)).
!array = !simulation.unpacked_array<0 : 1 x i8>
module {
  simulation.design @final_versions {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : !array design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "whole_copy"
    // CHECK-LABEL: regions @whole_copy valid=1
    // CHECK: values valid=1
    // CHECK-NEXT: value-input s0[0,16) s1[8,16)
    // CHECK-NEXT: final-input s0[0,16)
    simulation.func @whole_copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %scratch = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<!array>
      %zero = arith.constant 0 : i8
      %two = arith.constant 2 : i8
      %one = arith.constant 1 : i8
      cf.br ^head(%zero : i8)
    ^head(%i: i8):
      %more = arith.cmpi slt, %i, %two : i8
      cf.cond_br %more, ^body, ^wait
    ^body:
      %source = simulation.ref.array_element %input[%i] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %dest = simulation.ref.array_element %scratch[%i] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %source : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %dest : i8, !simulation.ref<i8>
      %all = simulation.ref.load %scratch : !simulation.ref<!array> -> !array
      simulation.ref.store %all to %output : !array, !simulation.ref<!array>
      %next = arith.addi %i, %one : i8
      cf.br ^head(%next : i8)
    ^wait:
      simulation.suspend.change %input to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^head(%zero : i8)
    }
  }
}
