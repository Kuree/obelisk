// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Fixed packed ranges use the declared source indices, including bounds that
// the prepare pass froze from elaborated parameters. Cover the write and read
// paths directly without involving the driver, scheduler, or execution tiers.

!logic = !obelisk.integral<1, false, true, 0 : 0, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!data = !obelisk.ranged_packed_array<31 : 0 x !logic>
!slice = !obelisk.ranged_packed_array<16 : 8 x !logic>

module {
  obelisk_sim.design @packed_range_select {
    obelisk_sim.code_unit.decl 9600001 in 0 initial
        hierarchy "top.packed_range_select"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<32>
        design hierarchy "top.data"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<9>
        design hierarchy "top.parameter_result"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<9>
        design hierarchy "top.literal_result"

    // CHECK-LABEL: obelisk_sim.func @constant_ranges
    // CHECK: %[[VALUE:.*]] = obelisk_sim.logic.constant -85 : i9, 0 : i9
    // CHECK: %[[PACKED_VALUE:.*]] = obelisk_sim.packed.unflatten %[[VALUE]]
    // CHECK: %[[WRITE:.*]] = obelisk_sim.ref.extract %arg1 from 8
    // CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.logic<32>>
    // CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.packed_array<16 : 8 x !obelisk_sim.logic<1>>>
    // CHECK: obelisk_sim.ref.store %[[PACKED_VALUE]] to %[[WRITE]]
    // CHECK: %[[PARAM_SOURCE:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[PARAM_BITS:.*]] = obelisk_sim.logic.extract %[[PARAM_SOURCE]] from 8
    // CHECK: %[[PARAM_PACKED:.*]] = obelisk_sim.packed.unflatten %[[PARAM_BITS]]
    // CHECK: %[[PARAM:.*]] = obelisk_sim.packed.flatten %[[PARAM_PACKED]]
    // CHECK: obelisk_sim.ref.store %[[PARAM]] to %arg2
    // CHECK: %[[LITERAL_SOURCE:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[LITERAL_BITS:.*]] = obelisk_sim.logic.extract %[[LITERAL_SOURCE]] from 8
    // CHECK: %[[LITERAL_PACKED:.*]] = obelisk_sim.packed.unflatten %[[LITERAL_BITS]]
    // CHECK: %[[LITERAL:.*]] = obelisk_sim.packed.flatten %[[LITERAL_PACKED]]
    // CHECK: obelisk_sim.ref.store %[[LITERAL]] to %arg3
    obelisk_sim.func @constant_ranges(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %data: !obelisk_sim.ref<!obelisk_sim.logic<32>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %parameter_result: !obelisk_sim.ref<!obelisk_sim.logic<9>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %literal_result: !obelisk_sim.ref<!obelisk_sim.logic<9>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.data", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.parameter_result",
                argument = 2, kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.literal_result",
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
                obelisk_sim.constant_value = "16"} {
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 6 : i64, referenced_path = "top.LOW",
                referenced_symbol = @LOW, semantic_type = !int,
                obelisk_sim.constant_value = "8"} {
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
                obelisk_sim.constant_value = "16"} {
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 14 : i64, referenced_path = "top.LOW",
                referenced_symbol = @LOW, semantic_type = !int,
                obelisk_sim.constant_value = "8"} {
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
      obelisk_sim.return
    }
  }
}
