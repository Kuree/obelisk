// RUN: talos-opt %s '--test-talos-activation-regions=reaching-values=true slice-root=5' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

// A and B are assigned before every read, but B is also selected together
// with incoming C. Reject B, then propagate that rejection through A/B.
// Reconstructing only some targets would mix private and canonical references
// and could discard source-local snapshots (IEEE 1800-2023 4.6).
// CHECK-LABEL: regions @incoming_selection valid=1
// CHECK: activation slice @incoming_selection
// CHECK-NOT: simulation.ref.alloc
// CHECK: arith.select
// CHECK-NOT: simulation.ref.alloc
// CHECK: arith.select
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.return
module {
  simulation.design @selected_closure {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "incoming_selection"
    simulation.func @incoming_selection(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %flag = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %a = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %b = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %incoming = simulation.context.storage %ctx[4] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %c = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %a : i8, !simulation.ref<i8>
      simulation.ref.store %v to %b : i8, !simulation.ref<i8>
      %ab = arith.select %c, %a, %b : !simulation.ref<i8>
      %first = simulation.ref.load %ab : !simulation.ref<i8> -> i8
      %bc = arith.select %c, %b, %incoming : !simulation.ref<i8>
      %second = simulation.ref.load %bc : !simulation.ref<i8> -> i8
      %sum = arith.addi %first, %second : i8
      simulation.ref.store %sum to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %flag, %incoming edges [0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>, !simulation.ref<i8>
    }
  }
}
