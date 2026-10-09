// RUN: obelisk-opt %s '--test-obelisk-activation-regions=reaching-values=true inputs=true slice-root=1' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// A reference select can read either canonical storage or a private slot.
// Both memory versions and the selector influence the publication. The slot
// store must survive executable slicing, even though the load also has a
// canonical read inventory entry.
module {
  simulation.design @mixed {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "select_slot"
    // CHECK-LABEL: regions @select_slot valid=1
    // CHECK-NEXT: live-in s0[0,8) s2[0,1) s3[0,8)
    // CHECK-NEXT: values valid=1
    // CHECK-NEXT: value-input s0[0,8) s2[0,1) s3[0,8)
    // CHECK-NEXT: final-input s0[0,8) s2[0,1) s3[0,8)
    // CHECK: activation slice @select_slot
    // CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
    // CHECK: %[[IN:.*]] = simulation.ref.load
    // CHECK: simulation.ref.store %[[IN]] to %[[LOCAL]]
    // CHECK: %[[SELECTED:.*]] = arith.select %{{.*}}, %[[LOCAL]], %{{.*}} : !simulation.ref<i8>
    // CHECK: %[[OUT:.*]] = simulation.ref.load %[[SELECTED]]
    // CHECK: simulation.ref.store %[[OUT]] to
    simulation.func @select_slot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %flag = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %other = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      cf.br ^body
    ^body:
      %slot = simulation.ref.alloc %zero : i8 -> !simulation.ref<i8>
      %v = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %slot : i8, !simulation.ref<i8>
      %c = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      %selected = arith.select %c, %slot, %other : !simulation.ref<i8>
      %result = simulation.ref.load %selected : !simulation.ref<i8> -> i8
      simulation.ref.store %result to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %flag, %other edges [0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>, !simulation.ref<i8>
    }
  }
}
