// RUN: obelisk-opt %s '--test-obelisk-activation-regions=publications=true slice-order=true slice-root=3' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// LRM 4.6: a later private load observes the preceding selected store.
// Neither target is canonical storage, so the store must remain a private
// definition rather than an empty design publication that slicing discards.
module {
  simulation.design @private_selection {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "private_store"
    // CHECK-LABEL: regions @private_store valid=1
    // CHECK: activation slice @private_store
    // CHECK: %[[A:.*]] = simulation.ref.alloc
    // CHECK: %[[B:.*]] = simulation.ref.alloc
    // CHECK: %[[INPUT:.*]] = simulation.ref.load
    // CHECK: %[[FLAG:.*]] = simulation.ref.load
    // CHECK: %[[SELECTED:.*]] = arith.select %[[FLAG]], %[[A]], %[[B]]
    // CHECK: simulation.ref.store %[[INPUT]] to %[[SELECTED]]
    // CHECK: %[[VALUE:.*]] = simulation.ref.load %[[A]]
    // CHECK: simulation.ref.store %[[VALUE]] to
    // CHECK: publication read:s0[0,8) read:s1[0,8) read:s2[0,1) write:s3[0,8)
    // CHECK-NOT: publication read:
    // CHECK: slice graph: acyclic
    // CHECK: publication graph: acyclic
    simulation.func @private_store(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %initial = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %flag = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      cf.br ^body
    ^body:
      %init = simulation.ref.load %initial : !simulation.ref<i8> -> i8
      %a = simulation.ref.alloc %init : i8 -> !simulation.ref<i8>
      %b = simulation.ref.alloc %zero : i8 -> !simulation.ref<i8>
      %v = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %c = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      %selected = arith.select %c, %a, %b : !simulation.ref<i8>
      simulation.ref.store %v to %selected : i8, !simulation.ref<i8>
      %result = simulation.ref.load %a : !simulation.ref<i8> -> i8
      simulation.ref.store %result to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %initial, %flag edges [0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i1>
    }
  }
}
