// RUN: talos-opt %s '--test-talos-activation-regions=inputs=true' -o /dev/null 2>&1 | FileCheck %s
// RUN: talos-opt %s '--test-talos-activation-regions=compare-input-dependencies=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=COMPARE

// The input-only dependency proof agrees with full publication analysis.
// COMPARE: inputs @join_ranges agree

// Join paths with partially overlapping intervals on multiple roots. A later
// whole-word read depends only on bits not definitely initialized on both paths.
// CHECK-LABEL: regions @join_ranges valid=1
// CHECK-NEXT: live-in s0[0,1) s1[0,4) s1[8,14){{$}}
// CHECK: must-write s1[4,8) s1[14,16) s2[0,16)
// CHECK-NEXT: may-write s1[0,16) s2[0,16)
module {
  simulation.design @range_joins {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i16 design
    simulation.storage.decl 2 in 0 : i16 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "join_ranges"
    simulation.func @join_ranges(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %condition = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %word = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %out = simulation.context.storage %ctx[2] : !simulation.ref<i16>
      %a0 = simulation.ref.extract %word from 0 : !simulation.ref<i16> -> !simulation.ref<i8>
      %a1 = simulation.ref.extract %word from 12 : !simulation.ref<i16> -> !simulation.ref<i4>
      %b0 = simulation.ref.extract %word from 4 : !simulation.ref<i16> -> !simulation.ref<i8>
      %b1 = simulation.ref.extract %word from 14 : !simulation.ref<i16> -> !simulation.ref<i2>
      %c8 = arith.constant 0 : i8
      %c4 = arith.constant 0 : i4
      %c2 = arith.constant 0 : i2
      cf.br ^body
    ^body:
      %c = simulation.ref.load %condition : !simulation.ref<i1> -> i1
      cf.cond_br %c, ^a, ^b
    ^a:
      simulation.ref.store %c4 to %a1 : i4, !simulation.ref<i4>
      simulation.ref.store %c8 to %a0 : i8, !simulation.ref<i8>
      cf.br ^join
    ^b:
      simulation.ref.store %c8 to %b0 : i8, !simulation.ref<i8>
      simulation.ref.store %c2 to %b1 : i2, !simulation.ref<i2>
      cf.br ^join
    ^join:
      %v = simulation.ref.load %word : !simulation.ref<i16> -> i16
      simulation.ref.store %v to %out : i16, !simulation.ref<i16>
      simulation.suspend.change %condition to ^body : !simulation.ref<i1>
    }
  }
}
