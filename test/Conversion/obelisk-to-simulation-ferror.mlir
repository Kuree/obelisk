// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// $ferror yields the descriptor's pending error code and stores the host
// message into the string destination.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: %[[FD:.*]] = obelisk_sim.file.open
// CHECK: %[[MESSAGE:.*]], %[[CODE:.*]] = obelisk_sim.file.error_string
// CHECK: obelisk_sim.ref.store %[[MESSAGE]]
// CHECK: obelisk_sim.logic.from_bits %[[CODE]]

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.fd", lifetime = 1 : i32, name = "fd", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s5.fd"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.message", lifetime = 1 : i32, name = "message", node_id = 6 : i64, semantic_type = !obelisk.string, sym_name = "s6.message"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.code", lifetime = 1 : i32, name = "code", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s7.code"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.fd", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.fd, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fopen", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.conversion attributes {node_id = 15 : i64, semantic_type = !obelisk.string} {
                      obelisk.sv.expression.string_literal attributes {constant_value = "scratch.log", node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 17 : i64, semantic_type = !obelisk.string} {
                      obelisk.sv.expression.string_literal attributes {constant_value = "w", node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.code", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$ferror", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.fd", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.fd, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 24 : i64, semantic_type = !obelisk.string} {
                      obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.message", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.message, semantic_type = !obelisk.string} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 26 : i64, semantic_type = !obelisk.string} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 27 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$fclose", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 28 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_path = "top.fd", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.fd, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
