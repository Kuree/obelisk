// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-instrument-reference-lifetimes)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-instrument-reference-lifetimes,obelisk-sim-instrument-reference-lifetimes)))' | FileCheck %s

module {
  obelisk_sim.design @reference_lifetimes {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "reference_lifetimes.direct"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "reference_lifetimes.loop"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "reference_lifetimes.branch"

    obelisk_sim.func @direct(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %initial: i64 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %local = obelisk_sim.ref.alloc %initial :
          i64 -> !obelisk_sim.ref<i64>
      obelisk_sim.return
    }

    obelisk_sim.func @loop(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %initial: i64 {obelisk_sim.capture_kind = 2 : i32},
        %repeat: i1 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      cf.br ^allocate
    ^allocate:
      %local = obelisk_sim.ref.alloc %initial :
          i64 -> !obelisk_sim.ref<i64>
      cf.br ^use(%local : !obelisk_sim.ref<i64>)
    ^use(%reference: !obelisk_sim.ref<i64>):
      %value = obelisk_sim.ref.load %reference :
          !obelisk_sim.ref<i64> -> i64
      cf.cond_br %repeat, ^allocate, ^done
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @branch(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %initial: i64 {obelisk_sim.capture_kind = 2 : i32},
        %use_reference: i1 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %local = obelisk_sim.ref.alloc %initial :
          i64 -> !obelisk_sim.ref<i64>
      cf.cond_br %use_reference,
          ^use(%local : !obelisk_sim.ref<i64>), ^done
    ^use(%reference: !obelisk_sim.ref<i64>):
      %value = obelisk_sim.ref.load %reference :
          !obelisk_sim.ref<i64> -> i64
      obelisk_sim.return
    ^done:
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @direct
// CHECK: obelisk_sim.ref.alloc
// CHECK-SAME: obelisk.owner_release_instrumented
// CHECK-NEXT: obelisk_sim.ref.release_owner
// CHECK-NEXT: obelisk_sim.return

// CHECK-LABEL: obelisk_sim.func @loop
// CHECK: obelisk_sim.ref.alloc
// CHECK-SAME: obelisk.owner_release_instrumented
// CHECK: obelisk_sim.ref.release_owner
// CHECK-NEXT: cf.cond_br

// CHECK-LABEL: obelisk_sim.func @branch
// CHECK: obelisk_sim.ref.alloc
// CHECK-SAME: obelisk.owner_release_instrumented
// CHECK-COUNT-2: obelisk_sim.ref.release_owner
