// RUN: obelisk-opt %s -canonicalize | FileCheck %s

// IEEE 1800-2023 11.4.6, 11.4.10, 11.4.12, and 11.5.1.
module {
  simulation.design @slices {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "disjoint"
    simulation.code_unit.decl 2 in 0 function hierarchy "cross_concat"
    simulation.code_unit.decl 3 in 0 function hierarchy "partial"
    // CHECK-LABEL: simulation.func @disjoint(
    // CHECK-SAME: %[[CTX:[^:]+]]: {{.*}}, %[[V:[^:]+]]: !simulation.logic<8>
    // CHECK-SAME: %[[B:[^:]+]]: i8
    // CHECK-DAG: %[[X:.*]] = simulation.logic.constant 0 : i4, -1 : i4
    // CHECK-DAG: %[[Z:.*]] = arith.constant 0 : i4
    // CHECK-DAG: %[[ZERO:.*]] = simulation.logic.constant 0 : i8, 0 : i8
    // CHECK-DAG: %[[TRUE:.*]] = simulation.logic.constant true, false
    // CHECK-NOT: dyn_extract
    // CHECK-NOT: dyn_insert
    // CHECK-NOT: logic.shift
    // CHECK-NOT: logic.compare
    // CHECK: simulation.return %[[X]], %[[X]], %[[Z]], %[[V]], %[[B]], %[[ZERO]], %[[TRUE]]
    simulation.func @disjoint(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %v: !simulation.logic<8> {simulation.capture_kind = 2 : i32}, %b: i8 {simulation.capture_kind = 2 : i32}, %r: !simulation.logic<4> {simulation.capture_kind = 2 : i32}, %rb: i4 {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<4>, !simulation.logic<4>, i4, !simulation.logic<8>, i8, !simulation.logic<8>, !simulation.logic<1>) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %high = arith.constant 8 : i128
      %low = arith.constant -4 : i128
      %huge = arith.constant 18446744073709551616 : i128
      %mask = simulation.logic.constant 85 : i8, -1 : i8 : !simulation.logic<8>
      %a = simulation.logic.dyn_extract %v from %high : (!simulation.logic<8>, i128) -> !simulation.logic<4>
      %c = simulation.logic.dyn_extract %v from %low : (!simulation.logic<8>, i128) -> !simulation.logic<4>
      %d = simulation.bits.dyn_extract %b from %huge : (i8, i128) -> i4
      %e = simulation.logic.dyn_insert %r into %v at %low : (!simulation.logic<8>, !simulation.logic<4>, i128) -> !simulation.logic<8>
      %f = simulation.bits.dyn_insert %rb into %b at %high : (i8, i4, i128) -> i8
      %g = simulation.logic.shift right %v by %huge : (!simulation.logic<8>, i128) -> !simulation.logic<8>
      %h = simulation.logic.compare wild_eq %v, %mask : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      simulation.return %a, %c, %d, %e, %f, %g, %h : !simulation.logic<4>, !simulation.logic<4>, i4, !simulation.logic<8>, i8, !simulation.logic<8>, !simulation.logic<1>
    }

    // A slice spanning three pieces keeps the middle one whole and selects
    // the boundary pieces independently. No arithmetic may erase their Z bits.
    // CHECK-LABEL: simulation.func @cross_concat
    // CHECK: %[[LOW:.*]] = simulation.logic.extract %{{.*}} from 2 : !simulation.logic<4> -> !simulation.logic<2>
    // CHECK: %[[HIGH:.*]] = simulation.logic.extract %{{.*}} from 0 : !simulation.logic<4> -> !simulation.logic<2>
    // CHECK: %[[CAT:.*]] = simulation.logic.concat %[[HIGH]], %{{.*}}, %[[LOW]] : (!simulation.logic<2>, !simulation.logic<4>, !simulation.logic<2>) -> !simulation.logic<8>
    // CHECK: simulation.return %[[CAT]]
    simulation.func @cross_concat(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.logic<4> {simulation.capture_kind = 2 : i32}, %b: !simulation.logic<4> {simulation.capture_kind = 2 : i32}, %c: !simulation.logic<4> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %cat = simulation.logic.concat %a, %b, %c : (!simulation.logic<4>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<12>
      %slice = simulation.logic.extract %cat from 2 : !simulation.logic<12> -> !simulation.logic<8>
      simulation.return %slice : !simulation.logic<8>
    }

    // Partially overlapping slices and arithmetic shifts still depend on data.
    // A wildcard on the LHS does not wildcard a known RHS.
    // CHECK-LABEL: simulation.func @partial
    // CHECK: simulation.logic.dyn_extract
    // CHECK: simulation.logic.dyn_insert
    // CHECK: simulation.logic.shift right_arith
    // CHECK: simulation.logic.compare wild_eq
    simulation.func @partial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %v: !simulation.logic<8> {simulation.capture_kind = 2 : i32}, %r: !simulation.logic<4> {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<4>, !simulation.logic<8>, !simulation.logic<8>, !simulation.logic<1>) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %low = arith.constant -1 : i32
      %high = arith.constant 8 : i32
      %mask = simulation.logic.constant 85 : i8, -1 : i8 : !simulation.logic<8>
      %a = simulation.logic.dyn_extract %v from %low : (!simulation.logic<8>, i32) -> !simulation.logic<4>
      %b = simulation.logic.dyn_insert %r into %v at %low : (!simulation.logic<8>, !simulation.logic<4>, i32) -> !simulation.logic<8>
      %c = simulation.logic.shift right_arith %v by %high : (!simulation.logic<8>, i32) -> !simulation.logic<8>
      %d = simulation.logic.compare wild_eq %mask, %v : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      simulation.return %a, %b, %c, %d : !simulation.logic<4>, !simulation.logic<8>, !simulation.logic<8>, !simulation.logic<1>
    }
  }
}
