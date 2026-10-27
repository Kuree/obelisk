// RUN: talos-opt %s '--test-talos-activation-regions=reaching-values=true publications=true' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

// A selected resolved-net read depends on both candidate nets and on the
// selector. Net and storage descriptor zero must remain distinct roots.
module {
  simulation.design @selected_nets {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : i8 design
    simulation.net.decl 1 in 0 : i8 design
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "selected"
    // CHECK-LABEL: regions @selected valid=1
    // CHECK: node simulation.net.read read:s4611686018427387904[0,8) read:s4611686018427387905[0,8)
    // CHECK: publication read:s0[0,1) read:s4611686018427387904[0,8) read:s4611686018427387905[0,8) write:s1[0,8)
    simulation.func @selected(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.net %ctx[0] : !simulation.net<i8>
      %b = simulation.context.net %ctx[1] : !simulation.net<i8>
      %choose = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %c = simulation.ref.load %choose : !simulation.ref<i1> -> i1
      %net = arith.select %c, %a, %b : !simulation.net<i8>
      %value = simulation.net.read %net : !simulation.net<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %a, %b, %choose edges [0, 0, 0] to ^body : !simulation.net<i8>, !simulation.net<i8>, !simulation.ref<i1>
    }
  }
  // One known arm cannot stand in for an unresolved formal net. Otherwise
  // the dependency proof would omit reads of whichever net the caller binds.
  simulation.design @unresolved_net {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : i8 design
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "unresolved_net"
    // CHECK-LABEL: regions @unresolved_net valid=0 reason=memory effects
    simulation.func @unresolved_net(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %unknown: !simulation.net<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %known = simulation.context.net %ctx[0] : !simulation.net<i8>
      %choose = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %c = simulation.ref.load %choose : !simulation.ref<i1> -> i1
      %net = arith.select %c, %known, %unknown : !simulation.net<i8>
      %value = simulation.net.read %net : !simulation.net<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %known, %unknown, %choose edges [0, 0, 0] to ^body : !simulation.net<i8>, !simulation.net<i8>, !simulation.ref<i1>
    }
  }
  // Reading a selected resolved net does not authorize treating a selected
  // driver publication as a definite write to both candidate nets. Even
  // isolated single-driver nets retain the exact-destination requirement.
  simulation.design @selected_driver {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : i8 design
    simulation.net.decl 1 in 0 : i8 design
    simulation.driver.decl 0 in 0 drives 0 : i8 design
    simulation.driver.decl 1 in 0 drives 1 : i8 design
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "selected_driver"
    // CHECK-LABEL: regions @selected_driver valid=0 reason=memory effects
    simulation.func @selected_driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.driver %ctx[0] : !simulation.driver<i8>
      %b = simulation.context.driver %ctx[1] : !simulation.driver<i8>
      %choose = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %c = simulation.ref.load %choose : !simulation.ref<i1> -> i1
      %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %driver = arith.select %c, %a, %b : !simulation.driver<i8>
      simulation.driver.drive %driver = %value : !simulation.driver<i8>, i8
      simulation.suspend.any %choose, %input edges [0, 0] to ^body : !simulation.ref<i1>, !simulation.ref<i8>
    }
  }
}
