// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_system_call", name = "unsupported_system_call", node_id = 0 : i64, sym_name = "s0.unsupported_system_call"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_system_call", is_uninstantiated = false, name = "unsupported_system_call", node_id = 3 : i64, referenced_path = "unsupported_system_call", referenced_symbol = @s0.unsupported_system_call, sym_name = "s3.unsupported_system_call"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_system_call", name = "unsupported_system_call", node_id = 4 : i64, sym_name = "s4.unsupported_system_call"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_system_call.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s5.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_system_call", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
              obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "unsupported_system_call.value", referenced_symbol = @s1.$root::@s3.unsupported_system_call::@s4.unsupported_system_call::@s5.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$dumpvars", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.unsupported_system_call", system_scope_path = "unsupported_system_call", system_scope_symbol = @s1.$root::@s3.unsupported_system_call::@s4.unsupported_system_call} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "unsupported_system_call.value", referenced_symbol = @s1.$root::@s3.unsupported_system_call::@s4.unsupported_system_call::@s5.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                }
                obelisk.sv.expression.string_literal attributes {constant_value = "%0d", node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "unsupported_system_call.value", referenced_symbol = @s1.$root::@s3.unsupported_system_call::@s4.unsupported_system_call::@s5.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
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

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: unsupported system call $dumpvars
