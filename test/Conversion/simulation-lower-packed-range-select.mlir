// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Fixed packed ranges use the declared source indices, including bounds that
// the prepare pass froze from elaborated parameters. Cover the write and read
// paths directly without involving the driver, scheduler, or execution tiers.

!logic = !obelisk.integral<1, false, true, 0 : 0, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!data = !obelisk.ranged_packed_array<31 : 0 x !logic>
!slice = !obelisk.ranged_packed_array<16 : 8 x !logic>

module {
  simulation.design @packed_range_select {
    simulation.code_unit.decl 9600001 in 0 initial
        hierarchy "top.packed_range_select"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<32>
        design hierarchy "top.data"
    simulation.storage.decl 1 in 0 : !simulation.logic<9>
        design hierarchy "top.parameter_result"
    simulation.storage.decl 2 in 0 : !simulation.logic<9>
        design hierarchy "top.literal_result"

    // CHECK-LABEL: simulation.func @constant_ranges
    // CHECK: %[[VALUE:.*]] = simulation.logic.constant -85 : i9, 0 : i9
    // CHECK: %[[PACKED_VALUE:.*]] = simulation.packed.unflatten %[[VALUE]]
    // CHECK: %[[WRITE:.*]] = simulation.ref.extract %arg1 from 8
    // CHECK-SAME: !simulation.ref<!simulation.logic<32>>
    // CHECK-SAME: !simulation.ref<!simulation.packed_array<16 : 8 x !simulation.logic<1>>>
    // CHECK: simulation.ref.store %[[PACKED_VALUE]] to %[[WRITE]]
    // CHECK: %[[PARAM_SOURCE:.*]] = simulation.ref.load %arg1
    // CHECK: %[[PARAM_BITS:.*]] = simulation.logic.extract %[[PARAM_SOURCE]] from 8
    // CHECK: %[[PARAM_PACKED:.*]] = simulation.packed.unflatten %[[PARAM_BITS]]
    // CHECK: %[[PARAM:.*]] = simulation.packed.flatten %[[PARAM_PACKED]]
    // CHECK: simulation.ref.store %[[PARAM]] to %arg2
    // CHECK: %[[LITERAL_SOURCE:.*]] = simulation.ref.load %arg1
    // CHECK: %[[LITERAL_BITS:.*]] = simulation.logic.extract %[[LITERAL_SOURCE]] from 8
    // CHECK: %[[LITERAL_PACKED:.*]] = simulation.packed.unflatten %[[LITERAL_BITS]]
    // CHECK: %[[LITERAL:.*]] = simulation.packed.flatten %[[LITERAL_PACKED]]
    // CHECK: simulation.ref.store %[[LITERAL]] to %arg3
    simulation.func @constant_ranges(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %data: !simulation.ref<!simulation.logic<32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %parameter_result: !simulation.ref<!simulation.logic<9>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %literal_result: !simulation.ref<!simulation.logic<9>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {
          entry_kind = 1 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.data", argument = 1,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.parameter_result",
                argument = 2, kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.literal_result",
                argument = 3, kind = direct, copyOut = false>
          ],
          code_unit_id = 9600001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 2 : i64, assignment_kind = 0 : i32,
            semantic_type = !slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 3 : i64, selection_kind = 0 : i32,
              semantic_type = !slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 4 : i64, referenced_path = "top.data",
                referenced_symbol = @data, semantic_type = !data} {
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 5 : i64, referenced_path = "top.HIGH",
                referenced_symbol = @HIGH, semantic_type = !int,
                simulation.constant_value = "16"} {
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 6 : i64, referenced_path = "top.LOW",
                referenced_symbol = @LOW, semantic_type = !int,
                simulation.constant_value = "8"} {
            }
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 7 : i64, constant_value = "9'h1ab",
              semantic_type = !slice} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 9 : i64, assignment_kind = 0 : i32,
            semantic_type = !slice} {
          obelisk.sv.expression.named_value attributes {
              node_id = 10 : i64, referenced_path = "top.parameter_result",
              referenced_symbol = @parameter_result,
              semantic_type = !slice} {
          }
          obelisk.sv.expression.range_select attributes {
              node_id = 11 : i64, selection_kind = 0 : i32,
              semantic_type = !slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 12 : i64, referenced_path = "top.data",
                referenced_symbol = @data, semantic_type = !data} {
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 13 : i64, referenced_path = "top.HIGH",
                referenced_symbol = @HIGH, semantic_type = !int,
                simulation.constant_value = "16"} {
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 14 : i64, referenced_path = "top.LOW",
                referenced_symbol = @LOW, semantic_type = !int,
                simulation.constant_value = "8"} {
            }
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {
          node_id = 15 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 16 : i64, assignment_kind = 0 : i32,
            semantic_type = !slice} {
          obelisk.sv.expression.named_value attributes {
              node_id = 17 : i64, referenced_path = "top.literal_result",
              referenced_symbol = @literal_result,
              semantic_type = !slice} {
          }
          obelisk.sv.expression.range_select attributes {
              node_id = 18 : i64, selection_kind = 0 : i32,
              semantic_type = !slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 19 : i64, referenced_path = "top.data",
                referenced_symbol = @data, semantic_type = !data} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 20 : i64, constant_value = "16",
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 21 : i64, constant_value = "8",
                semantic_type = !int} {
            }
          }
        }
      }
      simulation.return
    }
  }
}
