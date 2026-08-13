// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A design that never queries assertion control must not pay for it: no
// enabled/action_state material is emitted around either assertion.
//
//   module assertion_control_zero_overhead;
//     initial begin
//       assert (1'b1);
//       labeled: assert #0 (1'b1);
//       #1;
//       $finish;
//     end
//   endmodule

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "assertion_control_zero_overhead", name = "assertion_control_zero_overhead", node_id = 0 : i64, sym_name = "s0.assertion_control_zero_overhead"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "assertion_control_zero_overhead", is_uninstantiated = false, name = "assertion_control_zero_overhead", node_id = 3 : i64, referenced_path = "assertion_control_zero_overhead", referenced_symbol = @s0.assertion_control_zero_overhead, sym_name = "s3.assertion_control_zero_overhead"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "assertion_control_zero_overhead", name = "assertion_control_zero_overhead", node_id = 4 : i64, sym_name = "s4.assertion_control_zero_overhead", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_zero_overhead.labeled", name = "labeled", node_id = 5 : i64, sym_name = "s5.labeled"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assertion_control_zero_overhead", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.list attributes {node_id = 8 : i64} {
              obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = false, is_final = false, node_id = 9 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.statement.empty attributes {node_id = 11 : i64} {
                }
              }
              obelisk.sv.statement.block attributes {block_path = "assertion_control_zero_overhead.labeled", block_symbol = @s1.$root::@s3.assertion_control_zero_overhead::@s4.assertion_control_zero_overhead::@s5.labeled, node_id = 12 : i64} {
                obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = true, is_final = false, node_id = 13 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.statement.empty attributes {node_id = 15 : i64} {
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 16 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 17 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 19 : i64} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$finish", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 21 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_zero_overhead", system_scope_path = "assertion_control_zero_overhead", system_scope_symbol = @s1.$root::@s3.assertion_control_zero_overhead::@s4.assertion_control_zero_overhead} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-NOT: obelisk_sim.assert.enabled
// CHECK-NOT: obelisk_sim.assert.action_state
// CHECK: obelisk_sim.assert.deferred_enqueue
// CHECK-NOT: obelisk_sim.assert.enabled
// CHECK-NOT: obelisk_sim.assert.action_state
