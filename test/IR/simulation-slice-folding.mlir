// RUN: obelisk-opt %s -canonicalize | FileCheck %s

// IEEE 1800-2023 11.4.6, 11.4.10, 11.4.12, and 11.5.1.
module {
  obelisk_sim.design @slices {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "disjoint"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "cross_concat"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "partial"
    // CHECK-LABEL: obelisk_sim.func @disjoint(
    // CHECK-SAME: %[[CTX:[^:]+]]: {{.*}}, %[[V:[^:]+]]: !obelisk_sim.logic<8>
    // CHECK-SAME: %[[B:[^:]+]]: i8
    // CHECK-DAG: %[[X:.*]] = obelisk_sim.logic.constant 0 : i4, -1 : i4
    // CHECK-DAG: %[[Z:.*]] = arith.constant 0 : i4
    // CHECK-DAG: %[[ZERO:.*]] = obelisk_sim.logic.constant 0 : i8, 0 : i8
    // CHECK-DAG: %[[TRUE:.*]] = obelisk_sim.logic.constant true, false
    // CHECK-NOT: dyn_extract
    // CHECK-NOT: dyn_insert
    // CHECK-NOT: logic.shift
    // CHECK-NOT: logic.compare
    // CHECK: obelisk_sim.return %[[X]], %[[X]], %[[Z]], %[[V]], %[[B]], %[[ZERO]], %[[TRUE]]
    obelisk_sim.func @disjoint(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %v: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}, %b: i8 {obelisk_sim.capture_kind = 2 : i32}, %r: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}, %rb: i4 {obelisk_sim.capture_kind = 2 : i32}) -> (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>, i4, !obelisk_sim.logic<8>, i8, !obelisk_sim.logic<8>, !obelisk_sim.logic<1>) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %high = arith.constant 8 : i128
      %low = arith.constant -4 : i128
      %huge = arith.constant 18446744073709551616 : i128
      %mask = obelisk_sim.logic.constant 85 : i8, -1 : i8 : !obelisk_sim.logic<8>
      %a = obelisk_sim.logic.dyn_extract %v from %high : (!obelisk_sim.logic<8>, i128) -> !obelisk_sim.logic<4>
      %c = obelisk_sim.logic.dyn_extract %v from %low : (!obelisk_sim.logic<8>, i128) -> !obelisk_sim.logic<4>
      %d = obelisk_sim.bits.dyn_extract %b from %huge : (i8, i128) -> i4
      %e = obelisk_sim.logic.dyn_insert %r into %v at %low : (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>, i128) -> !obelisk_sim.logic<8>
      %f = obelisk_sim.bits.dyn_insert %rb into %b at %high : (i8, i4, i128) -> i8
      %g = obelisk_sim.logic.shift right %v by %huge : (!obelisk_sim.logic<8>, i128) -> !obelisk_sim.logic<8>
      %h = obelisk_sim.logic.compare wild_eq %v, %mask : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      obelisk_sim.return %a, %c, %d, %e, %f, %g, %h : !obelisk_sim.logic<4>, !obelisk_sim.logic<4>, i4, !obelisk_sim.logic<8>, i8, !obelisk_sim.logic<8>, !obelisk_sim.logic<1>
    }

    // A slice spanning three pieces keeps the middle one whole and selects
    // the boundary pieces independently. No arithmetic may erase their Z bits.
    // CHECK-LABEL: obelisk_sim.func @cross_concat
    // CHECK: %[[LOW:.*]] = obelisk_sim.logic.extract %{{.*}} from 2 : !obelisk_sim.logic<4> -> !obelisk_sim.logic<2>
    // CHECK: %[[HIGH:.*]] = obelisk_sim.logic.extract %{{.*}} from 0 : !obelisk_sim.logic<4> -> !obelisk_sim.logic<2>
    // CHECK: %[[CAT:.*]] = obelisk_sim.logic.concat %[[HIGH]], %{{.*}}, %[[LOW]] : (!obelisk_sim.logic<2>, !obelisk_sim.logic<4>, !obelisk_sim.logic<2>) -> !obelisk_sim.logic<8>
    // CHECK: obelisk_sim.return %[[CAT]]
    obelisk_sim.func @cross_concat(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %a: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}, %b: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}, %c: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %cat = obelisk_sim.logic.concat %a, %b, %c : (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<12>
      %slice = obelisk_sim.logic.extract %cat from 2 : !obelisk_sim.logic<12> -> !obelisk_sim.logic<8>
      obelisk_sim.return %slice : !obelisk_sim.logic<8>
    }

    // Partially overlapping slices and arithmetic shifts still depend on data.
    // A wildcard on the LHS does not wildcard a known RHS.
    // CHECK-LABEL: obelisk_sim.func @partial
    // CHECK: obelisk_sim.logic.dyn_extract
    // CHECK: obelisk_sim.logic.dyn_insert
    // CHECK: obelisk_sim.logic.shift right_arith
    // CHECK: obelisk_sim.logic.compare wild_eq
    obelisk_sim.func @partial(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %v: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}, %r: !obelisk_sim.logic<4> {obelisk_sim.capture_kind = 2 : i32}) -> (!obelisk_sim.logic<4>, !obelisk_sim.logic<8>, !obelisk_sim.logic<8>, !obelisk_sim.logic<1>) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %low = arith.constant -1 : i32
      %high = arith.constant 8 : i32
      %mask = obelisk_sim.logic.constant 85 : i8, -1 : i8 : !obelisk_sim.logic<8>
      %a = obelisk_sim.logic.dyn_extract %v from %low : (!obelisk_sim.logic<8>, i32) -> !obelisk_sim.logic<4>
      %b = obelisk_sim.logic.dyn_insert %r into %v at %low : (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>, i32) -> !obelisk_sim.logic<8>
      %c = obelisk_sim.logic.shift right_arith %v by %high : (!obelisk_sim.logic<8>, i32) -> !obelisk_sim.logic<8>
      %d = obelisk_sim.logic.compare wild_eq %mask, %v : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      obelisk_sim.return %a, %b, %c, %d : !obelisk_sim.logic<4>, !obelisk_sim.logic<8>, !obelisk_sim.logic<8>, !obelisk_sim.logic<1>
    }
  }
}
