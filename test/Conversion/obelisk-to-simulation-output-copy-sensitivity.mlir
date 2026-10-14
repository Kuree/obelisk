// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// IEEE 1800-2023 9.2.2.2.1 and 9.2.2.3: copy-out is a write in the
// caller. Preserve the pre-call snapshot, but exclude the copied-out actual
// from both always_comb and always_latch sensitivity.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: simulation.ref.load %[[SCRATCH:arg[0-9]+]]
// CHECK: simulation.ref.store
// CHECK: simulation.ref.load %[[SOURCE:arg[0-9]+]]
// CHECK: %[[RETURN:.*]] = simulation.call
// CHECK: simulation.ref.store %[[RETURN]] to %[[SCRATCH]]
// CHECK-NOT: simulation.suspend.any
// CHECK: simulation.suspend.change %[[SOURCE]]

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.copy_sensitivity attributes {definition_kind = 0 : i32, hierarchical_name = "copy_sensitivity", name = "copy_sensitivity", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.copy_sensitivity attributes {hierarchical_name = "copy_sensitivity", is_uninstantiated = false, name = "copy_sensitivity", node_id = 3 : i64, referenced_path = "copy_sensitivity", referenced_symbol = @s0.copy_sensitivity} {
      obelisk.sv.symbol.instance_body @s4.copy_sensitivity attributes {hierarchical_name = "copy_sensitivity", name = "copy_sensitivity", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "copy_sensitivity", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.source attributes {hierarchical_name = "copy_sensitivity.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s6.scratch attributes {hierarchical_name = "copy_sensitivity.scratch", lifetime = 1 : i32, name = "scratch", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s7.snapshot attributes {hierarchical_name = "copy_sensitivity.snapshot", lifetime = 1 : i32, name = "snapshot", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.subroutine @s8.assign_value attributes {hierarchical_name = "copy_sensitivity.assign_value", name = "assign_value", node_id = 8 : i64, semantic_type = !obelisk.subroutine<(!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "copy_sensitivity.assign_value.target", referenced_symbol = @s1.$root::@s3.copy_sensitivity::@s4.copy_sensitivity::@s8.assign_value::@s9.target, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "copy_sensitivity.assign_value.value", referenced_symbol = @s1.$root::@s3.copy_sensitivity::@s4.copy_sensitivity::@s8.assign_value::@s10.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
          obelisk.sv.symbol.formal_argument @s9.target attributes {direction = 1 : i32, hierarchical_name = "copy_sensitivity.assign_value.target", name = "target", node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
          }
          obelisk.sv.symbol.formal_argument @s10.value attributes {direction = 0 : i32, hierarchical_name = "copy_sensitivity.assign_value.value", name = "value", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "copy_sensitivity", node_id = 15 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 16 : i64} {
            obelisk.sv.statement.list attributes {node_id = 17 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "copy_sensitivity.snapshot", referenced_symbol = @s1.$root::@s3.copy_sensitivity::@s4.copy_sensitivity::@s7.snapshot, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "copy_sensitivity.scratch", referenced_symbol = @s1.$root::@s3.copy_sensitivity::@s4.copy_sensitivity::@s6.scratch, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "assign_value", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 23 : i64, referenced_path = "copy_sensitivity.assign_value", referenced_symbol = @s1.$root::@s3.copy_sensitivity::@s4.copy_sensitivity::@s8.assign_value, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "copy_sensitivity.scratch", referenced_symbol = @s1.$root::@s3.copy_sensitivity::@s4.copy_sensitivity::@s6.scratch, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "copy_sensitivity.source", referenced_symbol = @s1.$root::@s3.copy_sensitivity::@s4.copy_sensitivity::@s5.source, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
