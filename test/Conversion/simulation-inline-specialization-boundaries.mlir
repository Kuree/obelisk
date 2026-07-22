// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=2 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O2
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O3
// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{max-iterations=4294967296}))' 2>&1 | FileCheck %s --check-prefix=ITERATION-ERROR

module {
  obelisk_sim.design @specialization_boundaries {
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "test.special48"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "test.special49"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "test.special96"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "test.special97"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "test.caller"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func private @opaque(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32}

    // Nine calls at weight five plus one state read at weight three.
    obelisk_sim.func private @special48(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      %loaded = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @special49(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      %loaded = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %extra = arith.constant 0 : i32
      obelisk_sim.return %value : i32
    }

    // Eighteen calls and two state reads cost exactly 96.
    obelisk_sim.func private @special96(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      %loaded0 = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %loaded1 = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @special97(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @opaque(%ctx) : (!obelisk_sim.context) -> ()
      %loaded0 = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %loaded1 = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %extra = arith.constant 0 : i32
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %c48 = arith.constant 48 : i32
      %c49 = arith.constant 49 : i32
      %c96 = arith.constant 96 : i32
      %c97 = arith.constant 97 : i32
      %v48 = obelisk_sim.call @special48(%ctx, %ref, %c48) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>, i32) -> i32
      %v49 = obelisk_sim.call @special49(%ctx, %ref, %c49) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>, i32) -> i32
      %v96 = obelisk_sim.call @special96(%ctx, %ref, %c96) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>, i32) -> i32
      %v97 = obelisk_sim.call @special97(%ctx, %ref, %c97) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>, i32) -> i32
      obelisk_sim.return %v97 : i32
    }
  }
}

// O2-LABEL: obelisk_sim.func @caller
// O2-NOT: obelisk_sim.call @special48
// O2: obelisk_sim.call @special49
// O2: obelisk_sim.call @special96
// O2: obelisk_sim.call @special97

// O3-LABEL: obelisk_sim.func @caller
// O3-NOT: obelisk_sim.call @special{{(48|49|96)}}
// O3: obelisk_sim.call @special97

// ITERATION-ERROR: inliner max-iterations exceeds unsigned range
