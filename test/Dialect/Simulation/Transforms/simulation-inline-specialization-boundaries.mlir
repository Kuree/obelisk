// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=2 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O2
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O3
// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{max-iterations=4294967296}))' 2>&1 | FileCheck %s --check-prefix=ITERATION-ERROR

module {
  simulation.design @specialization_boundaries {
    simulation.code_unit.decl 1 in 0 function hierarchy "test.special48"
    simulation.code_unit.decl 2 in 0 function hierarchy "test.special49"
    simulation.code_unit.decl 3 in 0 function hierarchy "test.special96"
    simulation.code_unit.decl 4 in 0 function hierarchy "test.special97"
    simulation.code_unit.decl 5 in 0 function hierarchy "test.caller"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design

    simulation.func private @opaque(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32}

    // Nine calls at weight five plus one state read at weight three.
    simulation.func private @special48(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      %loaded = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %value : i32
    }

    simulation.func private @special49(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      %loaded = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %extra = arith.constant 0 : i32
      simulation.return %value : i32
    }

    // Eighteen calls and two state reads cost exactly 96.
    simulation.func private @special96(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      %loaded0 = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %loaded1 = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %value : i32
    }

    simulation.func private @special97(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      %loaded0 = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %loaded1 = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %extra = arith.constant 0 : i32
      simulation.return %value : i32
    }

    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %c48 = arith.constant 48 : i32
      %c49 = arith.constant 49 : i32
      %c96 = arith.constant 96 : i32
      %c97 = arith.constant 97 : i32
      %v48 = simulation.call @special48(%ctx, %ref, %c48) : (!simulation.context, !simulation.ref<!simulation.logic<8>>, i32) -> i32
      %v49 = simulation.call @special49(%ctx, %ref, %c49) : (!simulation.context, !simulation.ref<!simulation.logic<8>>, i32) -> i32
      %v96 = simulation.call @special96(%ctx, %ref, %c96) : (!simulation.context, !simulation.ref<!simulation.logic<8>>, i32) -> i32
      %v97 = simulation.call @special97(%ctx, %ref, %c97) : (!simulation.context, !simulation.ref<!simulation.logic<8>>, i32) -> i32
      simulation.return %v97 : i32
    }
  }
}

// O2-LABEL: simulation.func @caller
// O2-NOT: simulation.call @special48
// O2: simulation.call @special49
// O2: simulation.call @special96
// O2: simulation.call @special97

// O3-LABEL: simulation.func @caller
// O3-NOT: simulation.call @special{{(48|49|96)}}
// O3: simulation.call @special97

// ITERATION-ERROR: inliner max-iterations exceeds unsigned range
