// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.16.11 through 6.16.14 declare itoa, hextoa, octtoa, and
// bintoa alike -- "void <base>toa(integer i)" storing "the ASCII <base>
// representation of i" -- so only the radix separates them. `integer` is
// signed, which is why itoa renders -11 as "-11"; the other three describe
// the same value in another base and render it "-b", "-13", and "-1011",
// not the unsigned spelling of its two's-complement pattern.

module {
  obelisk.sv.symbol.definition @s0.string_radix_toa attributes {definition_kind = 0 : i32, hierarchical_name = "string_radix_toa", name = "string_radix_toa", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.string_radix_toa attributes {hierarchical_name = "string_radix_toa", is_uninstantiated = false, name = "string_radix_toa", node_id = 3 : i64, referenced_path = "string_radix_toa", referenced_symbol = @s0.string_radix_toa} {
      obelisk.sv.symbol.instance_body @s4.string_radix_toa attributes {hierarchical_name = "string_radix_toa", name = "string_radix_toa", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.s attributes {hierarchical_name = "string_radix_toa.s", lifetime = 1 : i32, name = "s", node_id = 5 : i64, semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.variable @s6.v attributes {hierarchical_name = "string_radix_toa.v", lifetime = 1 : i32, name = "v", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "string_radix_toa", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "itoa", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.string_radix_toa", system_scope_path = "string_radix_toa", system_scope_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "string_radix_toa.s", referenced_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa::@s5.s, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 13 : i64, referenced_path = "string_radix_toa.v", referenced_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa::@s6.v, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "hextoa", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.string_radix_toa", system_scope_path = "string_radix_toa", system_scope_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "string_radix_toa.s", referenced_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa::@s5.s, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "string_radix_toa.v", referenced_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa::@s6.v, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "octtoa", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.string_radix_toa", system_scope_path = "string_radix_toa", system_scope_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "string_radix_toa.s", referenced_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa::@s5.s, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 21 : i64, referenced_path = "string_radix_toa.v", referenced_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa::@s6.v, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "bintoa", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 23 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.string_radix_toa", system_scope_path = "string_radix_toa", system_scope_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "string_radix_toa.s", referenced_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa::@s5.s, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 25 : i64, referenced_path = "string_radix_toa.v", referenced_symbol = @s1.$root::@s3.string_radix_toa::@s4.string_radix_toa::@s6.v, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
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


// CHECK: simulation.string.format_integer {{.*}} radix = <decimal> signed = true
// CHECK: simulation.string.format_integer {{.*}} radix = <hex> signed = true
// CHECK: simulation.string.format_integer {{.*}} radix = <octal> signed = true
// CHECK: simulation.string.format_integer {{.*}} radix = <binary> signed = true
// CHECK-NOT: obelisk.sv.
