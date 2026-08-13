// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// $assertcontrol lock/unlock alone still lowers to an assert.control op, but
// leaves no runtime enable or action-state query behind.
//
//   module assertion_control_lock_zero_query;
//     initial begin
//       $assertcontrol(1, 2, 1, 0, target);
//       $assertcontrol(2, 2, 1, 0, target);
//       target: assert (1'b1);
//       $finish;
//     end
//   endmodule

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "assertion_control_lock_zero_query", name = "assertion_control_lock_zero_query", node_id = 0 : i64, sym_name = "s0.assertion_control_lock_zero_query"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "assertion_control_lock_zero_query", is_uninstantiated = false, name = "assertion_control_lock_zero_query", node_id = 3 : i64, referenced_path = "assertion_control_lock_zero_query", referenced_symbol = @s0.assertion_control_lock_zero_query, sym_name = "s3.assertion_control_lock_zero_query"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "assertion_control_lock_zero_query", name = "assertion_control_lock_zero_query", node_id = 4 : i64, sym_name = "s4.assertion_control_lock_zero_query", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_lock_zero_query.target", name = "target", node_id = 5 : i64, sym_name = "s5.target"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assertion_control_lock_zero_query", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.list attributes {node_id = 8 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 5 : i64, callee_name = "$assertcontrol", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 10 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_lock_zero_query", system_scope_path = "assertion_control_lock_zero_query", system_scope_symbol = @s1.$root::@s3.assertion_control_lock_zero_query::@s4.assertion_control_lock_zero_query} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 15 : i64, referenced_path = "assertion_control_lock_zero_query.target", referenced_symbol = @s1.$root::@s3.assertion_control_lock_zero_query::@s4.assertion_control_lock_zero_query::@s5.target, semantic_type = !obelisk.void} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 5 : i64, callee_name = "$assertcontrol", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 17 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_lock_zero_query", system_scope_path = "assertion_control_lock_zero_query", system_scope_symbol = @s1.$root::@s3.assertion_control_lock_zero_query::@s4.assertion_control_lock_zero_query} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 22 : i64, referenced_path = "assertion_control_lock_zero_query.target", referenced_symbol = @s1.$root::@s3.assertion_control_lock_zero_query::@s4.assertion_control_lock_zero_query::@s5.target, semantic_type = !obelisk.void} {
                  }
                }
              }
              obelisk.sv.statement.block attributes {block_path = "assertion_control_lock_zero_query.target", block_symbol = @s1.$root::@s3.assertion_control_lock_zero_query::@s4.assertion_control_lock_zero_query::@s5.target, node_id = 23 : i64} {
                obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = false, is_final = false, node_id = 24 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.statement.empty attributes {node_id = 26 : i64} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 27 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$finish", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 28 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_lock_zero_query", system_scope_path = "assertion_control_lock_zero_query", system_scope_symbol = @s1.$root::@s3.assertion_control_lock_zero_query::@s4.assertion_control_lock_zero_query} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.assert.control
// CHECK-NOT: obelisk_sim.assert.enabled
// CHECK-NOT: obelisk_sim.assert.action_state
