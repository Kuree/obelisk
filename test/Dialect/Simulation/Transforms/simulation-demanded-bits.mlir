// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off},simulation.func(canonicalize)))' | FileCheck %s
module {
  simulation.design @demand {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "low"
    simulation.code_unit.decl 2 in 0 function hierarchy "whole"
    // The low bits are fixed even when the unused high bits carry X/Z.
    // Backward demand reaches through the CFG join and both bitwise producers.
    // CHECK-LABEL: simulation.func @low
    // CHECK-NOT: simulation.logic.binary
    // CHECK: simulation.logic.constant 0 : i8, 0 : i8
    // CHECK: simulation.return
    simulation.func @low(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.logic<16> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %mask = simulation.logic.constant 65280 : i16, 0 : i16 : !simulation.logic<16>
      %a = simulation.logic.binary and %input, %mask : !simulation.logic<16>
      cf.cond_br %choose, ^left, ^right
    ^left:
      cf.br ^join(%a : !simulation.logic<16>)
    ^right:
      cf.br ^join(%a : !simulation.logic<16>)
    ^join(%v: !simulation.logic<16>):
      %low = simulation.logic.extract %v from 0 : !simulation.logic<16> -> !simulation.logic<8>
      simulation.return %low : !simulation.logic<8>
    }
    // Observing the whole value preserves its high unknown bits.
    // CHECK-LABEL: simulation.func @whole
    // CHECK: simulation.logic.binary and
    simulation.func @whole(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.logic<16> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<16> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %mask = simulation.logic.constant 65280 : i16, 0 : i16 : !simulation.logic<16>
      %a = simulation.logic.binary and %input, %mask : !simulation.logic<16>
      simulation.return %a : !simulation.logic<16>
    }
    // A standard SSA select preserves sparse forward facts and backward
    // demand after a known-condition logic.mux is canonicalized.
    simulation.code_unit.decl 3 in 0 function hierarchy "selected_low"
    // CHECK-LABEL: simulation.func @selected_low
    // CHECK-NOT: simulation.logic.binary
    // CHECK-NOT: arith.select
    // CHECK: simulation.logic.constant 0 : i8, 0 : i8
    // CHECK: simulation.return
    simulation.func @selected_low(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.logic<16> {simulation.capture_kind = 2 : i32}, %other: !simulation.logic<16> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %mask = simulation.logic.constant 65280 : i16, 0 : i16 : !simulation.logic<16>
      %a = simulation.logic.binary and %input, %mask : !simulation.logic<16>
      %b = simulation.logic.binary and %other, %mask : !simulation.logic<16>
      %chosen = arith.select %choose, %a, %b : !simulation.logic<16>
      %low = simulation.logic.extract %chosen from 0 : !simulation.logic<16> -> !simulation.logic<8>
      simulation.return %low : !simulation.logic<8>
    }
    simulation.code_unit.decl 4 in 0 function hierarchy "selected_whole"
    // Observing both planes of the whole value must retain unknown high bits.
    // CHECK-LABEL: simulation.func @selected_whole
    // CHECK: simulation.logic.binary and
    // CHECK: simulation.logic.binary and
    // CHECK: arith.select
    // CHECK: simulation.return
    simulation.func @selected_whole(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.logic<16> {simulation.capture_kind = 2 : i32}, %other: !simulation.logic<16> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<16> attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %mask = simulation.logic.constant 65280 : i16, 0 : i16 : !simulation.logic<16>
      %a = simulation.logic.binary and %input, %mask : !simulation.logic<16>
      %b = simulation.logic.binary and %other, %mask : !simulation.logic<16>
      %chosen = arith.select %choose, %a, %b : !simulation.logic<16>
      simulation.return %chosen : !simulation.logic<16>
    }
  }
}
