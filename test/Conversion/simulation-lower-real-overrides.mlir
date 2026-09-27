// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Procedural assign/deassign applies to variables of integral or real type.
// Keep the source-to-simulation boundary honest for the real-valued case.

module {
  simulation.design @real_overrides {
    simulation.code_unit.decl 9930001 in 0 function
        hierarchy "top.real_overrides"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : f64 design
        hierarchy "top.real_state"

    // CHECK-LABEL: simulation.func @mutate
    // CHECK: %[[VALUE:.*]] = arith.constant 1.250000e+00 : f64
    // CHECK: simulation.override %{{.*}} = %[[VALUE]] assign true : !simulation.ref<f64>, f64
    // CHECK: simulation.release_override %{{.*}} assign true : !simulation.ref<f64>
    simulation.func @mutate(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %real_state: !simulation.ref<f64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 8 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.real_state", argument = 1,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9930001 : i64,
          simulation.void_function
        } {
      obelisk.sv.statement.procedural_assign attributes {
          is_force = false, node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            assignment_kind = 0 : i32, node_id = 2 : i64,
            semantic_type = !obelisk.real} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.real_state",
              referenced_symbol = @real_state,
              semantic_type = !obelisk.real} {
          }
          obelisk.sv.expression.real_literal attributes {
              constant_value = "1.25", node_id = 4 : i64,
              semantic_type = !obelisk.real} {
          }
        }
      }
      obelisk.sv.statement.procedural_deassign attributes {
          is_release = false, node_id = 5 : i64} {
        obelisk.sv.expression.named_value attributes {
            node_id = 6 : i64, referenced_path = "top.real_state",
            referenced_symbol = @real_state,
            semantic_type = !obelisk.real} {
        }
      }
      simulation.return
    }
  }
}
