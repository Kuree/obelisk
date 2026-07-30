// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_real_dpi", name = "unsupported_real_dpi", node_id = 0 : i64, sym_name = "s0.unsupported_real_dpi"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_real_dpi", is_uninstantiated = false, name = "unsupported_real_dpi", node_id = 3 : i64, referenced_path = "unsupported_real_dpi", referenced_symbol = @s0.unsupported_real_dpi, sym_name = "s3.unsupported_real_dpi"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_real_dpi", name = "unsupported_real_dpi", node_id = 4 : i64, sym_name = "s4.unsupported_real_dpi"} {
        obelisk.sv.symbol.subroutine attributes {dpi_c_identifier = "pass_real", hierarchical_name = "unsupported_real_dpi.pass_real", is_dpi_import, name = "pass_real", node_id = 5 : i64, semantic_type = !obelisk.subroutine<(!obelisk.real) -> !obelisk.real, false>, subroutine_kind = 0 : i32, sym_name = "s5.pass_real", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "unsupported_real_dpi.pass_real.value", name = "value", node_id = 7 : i64, semantic_type = !obelisk.real, sym_name = "s6.value"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_real_dpi.value", lifetime = 1 : i32, name = "value", node_id = 8 : i64, semantic_type = !obelisk.real, sym_name = "s7.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_real_dpi", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, semantic_type = !obelisk.real} {
              obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "unsupported_real_dpi.value", referenced_symbol = @s1.$root::@s3.unsupported_real_dpi::@s4.unsupported_real_dpi::@s7.value, semantic_type = !obelisk.real} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "pass_real", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 13 : i64, referenced_path = "unsupported_real_dpi.pass_real", referenced_symbol = @s1.$root::@s3.unsupported_real_dpi::@s4.unsupported_real_dpi::@s5.pass_real, semantic_type = !obelisk.real, subroutine_kind = 0 : i32} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "unsupported_real_dpi.value", referenced_symbol = @s1.$root::@s3.unsupported_real_dpi::@s4.unsupported_real_dpi::@s7.value, semantic_type = !obelisk.real} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: DPI imports support only scalar predefined integers
