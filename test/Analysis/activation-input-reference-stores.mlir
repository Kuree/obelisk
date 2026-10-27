// RUN: talos-opt %s '--test-talos-activation-regions=input-dependencies=true inputs=true' -o /dev/null 2>&1 | FileCheck %s
// RUN: talos-opt %s '--test-talos-activation-regions=reaching-values=true publications=true slice-order=true slice-root=4' -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLICE
// RUN: talos-opt %s '--test-talos-activation-regions=input-dependencies=true publications=true slice-order=true slice-root=4' -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLICE
// A selected store weakly updates private scratch and may publish canonical
// state. Its value, selector, and the old values of both possible destinations
// remain dependencies. A later definite store makes canonical writes complete.
// LRM 4.6: publishing the private result must observe the preceding selected
// store. Reject publication slicing until it can retain this coupling. The
// old slice silently returned the initializer for both values of the flag.
// SLICE: regions @select_store valid=1
// SLICE-NOT: activation slice
// SLICE: slice graph: unavailable
// SLICE: publication graph: unavailable
module {
  simulation.design @mixed_store {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "select_store"
    // CHECK-LABEL: regions @select_store valid=1
    // CHECK: inputs valid=1
    // CHECK-NEXT: value-input s0[0,8) s1[0,8) s2[0,1) s3[0,8){{$}}
    // CHECK: must-write s3[0,8) s4[0,8) s5[0,8){{$}}
    simulation.func @select_store(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %initial = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %flag = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %other = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %private_result = simulation.context.storage %ctx[4] : !simulation.ref<i8>
      %canonical_result = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      cf.br ^body
    ^body:
      %init = simulation.ref.load %initial : !simulation.ref<i8> -> i8
      %slot = simulation.ref.alloc %init : i8 -> !simulation.ref<i8>
      %v = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %c = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      %selected = arith.select %c, %slot, %other : !simulation.ref<i8>
      simulation.ref.store %v to %selected : i8, !simulation.ref<i8>
      %private = simulation.ref.load %slot : !simulation.ref<i8> -> i8
      %canonical = simulation.ref.load %other : !simulation.ref<i8> -> i8
      simulation.ref.store %private to %private_result : i8, !simulation.ref<i8>
      simulation.ref.store %canonical to %canonical_result : i8, !simulation.ref<i8>
      simulation.ref.store %zero to %other : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %initial, %flag edges [0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i1>
    }
  }
}
