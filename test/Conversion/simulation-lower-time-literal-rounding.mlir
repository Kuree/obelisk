// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 5.8: a time literal is rounded to the lexical time
// precision even when used as a realtime expression. Drive only unit lowering;
// the input literal is already scaled to the lexical 1ns time unit.

module {
  obelisk_sim.design @time_literal_rounding {
    obelisk_sim.code_unit.decl 9902001 in 0 function
        hierarchy "top.time_literal_rounding"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : f64 design hierarchy "top.result"

    // CHECK-LABEL: obelisk_sim.func @round_half
    // CHECK: %[[ROUNDED:.*]] = arith.constant 1.000000e-03 : f64
    // CHECK: obelisk_sim.ref.store %[[ROUNDED]] to %arg1
    obelisk_sim.func @round_half(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %result: !obelisk_sim.ref<f64>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 8 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.result", argument = 1,
                kind = direct, copyOut = false>
          ],
          obelisk_sim.delay_quantum = 1 : i64,
          obelisk_sim.delay_scale = 1000 : i64,
          code_unit_id = 9902001 : i64,
          obelisk_sim.void_function
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
      obelisk_sim.return
    }
  }
}
