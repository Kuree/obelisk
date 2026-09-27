// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 5.8: a time literal is rounded to the lexical time
// precision even when used as a realtime expression. Drive only unit lowering;
// the input literal is already scaled to the lexical 1ns time unit.

module {
  simulation.design @time_literal_rounding {
    simulation.code_unit.decl 9902001 in 0 function
        hierarchy "top.time_literal_rounding"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : f64 design hierarchy "top.result"

    // CHECK-LABEL: simulation.func @round_half
    // CHECK: %[[ROUNDED:.*]] = arith.constant 1.000000e-03 : f64
    // CHECK: simulation.ref.store %[[ROUNDED]] to %arg1
    simulation.func @round_half(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %result: !simulation.ref<f64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 8 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.result", argument = 1,
                kind = direct, copyOut = false>
          ],
          simulation.delay_quantum = 1 : i64,
          simulation.delay_scale = 1000 : i64,
          code_unit_id = 9902001 : i64,
          simulation.void_function
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            assignment_kind = 0 : i32, is_signed = false, node_id = 2 : i64,
            semantic_type = !obelisk.realtime} {
          obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 3 : i64,
              referenced_path = "top.result", referenced_symbol = @result,
              semantic_type = !obelisk.realtime} {
          }
          obelisk.sv.expression.time_literal attributes {
              constant_value = "0.00050000000000000001",
              is_signed = false, node_id = 4 : i64,
              semantic_type = !obelisk.realtime,
              time_scale = "1ns / 1ps"} {
          }
        }
      }
      simulation.return
    }
  }
}
