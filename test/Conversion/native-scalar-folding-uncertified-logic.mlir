// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(fold-native-scalar-values)))' | FileCheck %s

// A logic range certifies that the value holds no X or Z. A select between a
// converted integer and an X constant may produce X, so neither the select nor
// a slice of it has a range, and nothing may be replaced by a constant. A
// slice of a certified value is a shift and truncation of its range.
module {
  simulation.design @uncertified {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "select_with_x"
    simulation.code_unit.decl 2 in 0 function hierarchy "known_slice"

    // CHECK-LABEL: simulation.func @select_with_x
    // CHECK: %[[SEL:.*]] = arith.select
    // CHECK: %[[LOW:.*]] = simulation.logic.extract %[[SEL]] from 0
    // CHECK: %[[MID:.*]] = simulation.logic.extract %[[SEL]] from 8
    // CHECK: simulation.return %[[LOW]], %[[MID]]
    simulation.func @select_with_x(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %c: i1 {simulation.capture_kind = 2 : i32}, %bits: i80 {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<16>, !simulation.logic<16>) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %value = simulation.logic.from_bits %bits : i80 -> !simulation.logic<80>
      %x = simulation.logic.constant 0 : i80, -1 : i80 : !simulation.logic<80>
      %sel = arith.select %c, %value, %x : !simulation.logic<80>
      %low = simulation.logic.extract %sel from 0 : !simulation.logic<80> -> !simulation.logic<16>
      %mid = simulation.logic.extract %sel from 8 : !simulation.logic<80> -> !simulation.logic<16>
      simulation.return %low, %mid : !simulation.logic<16>, !simulation.logic<16>
    }

    // CHECK-LABEL: simulation.func @known_slice
    // CHECK: %[[C:.*]] = simulation.logic.constant 3 : i4, 0 : i4
    // CHECK: simulation.return %[[C]]
    simulation.func @known_slice(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %bits: i4 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<4> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %mask = arith.constant 3 : i4
      %low = arith.andi %bits, %mask : i4
      %high = arith.constant 48 : i8
      %wide = arith.extui %low : i4 to i8
      %joined = arith.ori %wide, %high : i8
      %value = simulation.logic.from_bits %joined : i8 -> !simulation.logic<8>
      %slice = simulation.logic.extract %value from 4 : !simulation.logic<8> -> !simulation.logic<4>
      simulation.return %slice : !simulation.logic<4>
    }
  }
}
