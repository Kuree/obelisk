// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-instrument-reference-lifetimes)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-instrument-reference-lifetimes,obelisk-sim-instrument-reference-lifetimes)))' | FileCheck %s

module {
  simulation.design @reference_lifetimes {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "reference_lifetimes.direct"
    simulation.code_unit.decl 2 in 0 function hierarchy "reference_lifetimes.loop"
    simulation.code_unit.decl 3 in 0 function hierarchy "reference_lifetimes.branch"

    simulation.func @direct(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %local = simulation.ref.alloc %initial :
          i64 -> !simulation.ref<i64>
      simulation.return
    }

    simulation.func @loop(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32},
        %repeat: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      cf.br ^allocate
    ^allocate:
      %local = simulation.ref.alloc %initial :
          i64 -> !simulation.ref<i64>
      cf.br ^use(%local : !simulation.ref<i64>)
    ^use(%reference: !simulation.ref<i64>):
      %value = simulation.ref.load %reference :
          !simulation.ref<i64> -> i64
      cf.cond_br %repeat, ^allocate, ^done
    ^done:
      simulation.return
    }

    simulation.func @branch(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32},
        %use_reference: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %local = simulation.ref.alloc %initial :
          i64 -> !simulation.ref<i64>
      cf.cond_br %use_reference,
          ^use(%local : !simulation.ref<i64>), ^done
    ^use(%reference: !simulation.ref<i64>):
      %value = simulation.ref.load %reference :
          !simulation.ref<i64> -> i64
      simulation.return
    ^done:
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @direct
// CHECK: simulation.ref.alloc
// CHECK-SAME: obelisk.owner_release_instrumented
// CHECK-NEXT: simulation.ref.release_owner
// CHECK-NEXT: simulation.return

// CHECK-LABEL: simulation.func @loop
// CHECK: simulation.ref.alloc
// CHECK-SAME: obelisk.owner_release_instrumented
// CHECK: simulation.ref.release_owner
// CHECK-NEXT: cf.cond_br

// CHECK-LABEL: simulation.func @branch
// CHECK: simulation.ref.alloc
// CHECK-SAME: obelisk.owner_release_instrumented
// CHECK-COUNT-2: simulation.ref.release_owner
