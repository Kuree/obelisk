// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// $test$plusargs matches whatever text it is handed; $value$plusargs splits
// its literal format so the runtime matches the name and the conversion is
// picked here. A miss leaves the destination alone, so the store selects
// between the parsed value and the one already there.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: obelisk_sim.plusarg.test
// CHECK: %[[PREFIX:.*]] = obelisk_sim.string.literal "SEED="
// CHECK: %[[TAIL:.*]], %[[FOUND:.*]] = obelisk_sim.plusarg.value {{.*}}, %[[PREFIX]]
// CHECK: %[[PARSED:.*]] = obelisk_sim.string.parse_integer %[[TAIL]] radix = 10
// CHECK: %[[CURRENT:.*]] = obelisk_sim.ref.load
// CHECK: %[[MATCHED:.*]] = arith.cmpi ne, %[[FOUND]]
// CHECK: arith.select %[[MATCHED]], {{.*}}, %[[CURRENT]]

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s5.value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.name", lifetime = 1 : i32, name = "name", node_id = 6 : i64, semantic_type = !obelisk.string, sym_name = "s6.name"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, semantic_type = !obelisk.string} {
                  obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.name", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.name, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 13 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "TRACE", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<39 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 15 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$test$plusargs", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.name", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.name, semantic_type = !obelisk.string} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 23 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$value$plusargs", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "SEED=%d", node_id = 25 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                    obelisk.sv.expression.binary_op attributes {node_id = 32 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      }
                      obelisk.sv.expression.conversion attributes {node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
