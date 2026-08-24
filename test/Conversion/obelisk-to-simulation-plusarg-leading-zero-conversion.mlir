// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 21.6: the user string of $value$plusargs is a plusarg prefix
// followed by a conversion, and uppercase, lowercase, and leading `0` forms of
// that conversion are all valid.  `%0d` therefore names the same decimal
// conversion `%d` does, over the same `N=` prefix.

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "t", name = "t",
    node_id = 0 : i64, sym_name = "s0.t"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t,
      sym_name = "s3.t"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "t", name = "t", node_id = 4 : i64,
        sym_name = "s4.t", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.v", lifetime = 1 : i32, name = "v",
          node_id = 5 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s5.v"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "t", node_id = 6 : i64, procedure_kind = 0 : i32,
          sym_name = "s6", time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.conditional attributes {
              check_kind = 0 : i32, condition_count = 1 : i64,
              condition_pattern_flags = array<i64: 0>, has_else = false,
              node_id = 8 : i64
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 2 : i64, callee_name = "$value$plusargs",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0>,
                has_inline_constraints = false,
                has_iterator_expression = false, has_output_arguments = true,
                has_this_class = false, is_signed = true,
                is_super_class = false, is_system_call = true,
                node_id = 9 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                subroutine_kind = 0 : i32, system_library_cell = "work.t",
                system_scope_path = "t",
                system_scope_symbol = @s1.$root::@s3.t::@s4.t
              } {
                obelisk.sv.expression.string_literal attributes {
                  constant_value = "N=%0d", is_signed = false,
                  node_id = 10 : i64,
                  semantic_type = !obelisk.ranged_packed_array<39 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                }
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = true,
                  node_id = 11 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 12 : i64,
                    referenced_path = "t.v",
                    referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.v,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                  obelisk.sv.expression.empty_argument attributes {
                    is_signed = true, node_id = 13 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {
                node_id = 14 : i64
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$display",
                  constraint_restrictions = [],
                  defaulted_arguments = array<i64: 0, 0>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_signed = false, is_super_class = false,
                  is_system_call = true, node_id = 15 : i64,
                  semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
                  system_library_cell = "work.t", system_scope_path = "t",
                  system_scope_symbol = @s1.$root::@s3.t::@s4.t
                } {
                  obelisk.sv.expression.string_literal attributes {
                    constant_value = "%0d", is_signed = false,
                    node_id = 16 : i64,
                    semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                  } {
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 17 : i64,
                    referenced_path = "t.v",
                    referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.v,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
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



// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[PREFIX:.*]] = obelisk_sim.string.literal "N="
// CHECK: %[[TAIL:.*]], %[[FOUND:.*]] = obelisk_sim.plusarg.value %{{.*}}, %[[PREFIX]]
// CHECK: obelisk_sim.string.parse_logic %[[TAIL]] radix = 10 : <64>
