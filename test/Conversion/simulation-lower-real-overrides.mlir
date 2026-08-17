// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Procedural assign/deassign applies to variables of integral or real type.
// Keep the source-to-simulation boundary honest for the real-valued case.

module {
  obelisk_sim.design @real_overrides {
    obelisk_sim.code_unit.decl 9930001 in 0 function
        hierarchy "top.real_overrides"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : f64 design
        hierarchy "top.real_state"

    // CHECK-LABEL: obelisk_sim.func @mutate
    // CHECK: %[[VALUE:.*]] = arith.constant 1.250000e+00 : f64
    // CHECK: obelisk_sim.override %{{.*}} = %[[VALUE]] assign true : !obelisk_sim.ref<f64>, f64
    // CHECK: obelisk_sim.release_override %{{.*}} assign true : !obelisk_sim.ref<f64>
    obelisk_sim.func @mutate(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %real_state: !obelisk_sim.ref<f64>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 8 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.real_state", argument = 1,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9930001 : i64,
          obelisk_sim.void_function
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
      obelisk_sim.return
    }
  }
}
