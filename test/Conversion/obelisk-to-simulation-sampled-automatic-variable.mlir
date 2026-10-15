// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 16.9.3: $past samples `data` at every tick of the inferred
// clock, which a sampler process records; the loop evaluates the call twice
// per tick. The automatic index is read at its current value (16.5.1).
//
//   module top;
//     bit clk;
//     bit [1:0] data;
//     bit seen;
//     always @(posedge clk)
//       for (int i = 0; i < 2; i++) seen = $past(data[i]);
//   endmodule

// CHECK: simulation.func private @unit_0.$clocked_sample.[[SITE:[0-9]+]](
// CHECK: simulation.suspend.edge posedge %arg2
// CHECK-SAME: resume_region = 16
// CHECK: %[[SAMPLE:.*]] = simulation.assert.sampled_read %arg0 from %arg1 : (!simulation.context, !simulation.ref<!simulation.packed_array<1 : 0 x i1>>)
// CHECK: simulation.assert.clocked_sample_update %arg0 from %[[SAMPLE]] {{.*}} id [[SITE]] depth 1
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-NOT: simulation.assert.sampled
// CHECK: %[[PAST:.*]] = simulation.assert.clocked_sample_read %arg0 id [[SITE]] depth 1 age 0
// CHECK-NEXT: %[[INDEX:.*]] = arith.extsi %{{.*}} : i32 to i65
// CHECK-NEXT: simulation.array.extract_dynamic %[[PAST]][%[[INDEX]]]
// CHECK-NOT: simulation.assert.sampled

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s6.data attributes {hierarchical_name = "top.data", lifetime = 1 : i32, name = "data", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
        }
        obelisk.sv.symbol.variable @s7.seen attributes {hierarchical_name = "top.seen", lifetime = 1 : i32, name = "seen", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.statement_block @s8 attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 8 : i64} {
          obelisk.sv.symbol.variable @s9.i attributes {hierarchical_name = "top.i", name = "i", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 12 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
            obelisk.sv.statement.block attributes {node_id = 15 : i64} {
              obelisk.sv.statement.list attributes {node_id = 16 : i64} {
                obelisk.sv.statement.variable_declaration attributes {node_id = 17 : i64, referenced_path = "top.i", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s9.i} {
                }
                obelisk.sv.statement.for_loop attributes {has_condition = true, initializer_count = 0 : i64, node_id = 18 : i64, step_count = 1 : i64} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 19 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 20 : i64, referenced_path = "top.i", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s9.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 22 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 23 : i64, referenced_path = "top.i", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s9.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.seen", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.seen, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      }
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$past", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s8} {
                        obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "top.data", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.data, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 30 : i64, referenced_path = "top.i", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s9.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
      }
    }
  }
}

