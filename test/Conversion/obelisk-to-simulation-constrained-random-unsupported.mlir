// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=UNSUPPORTED

// UNSUPPORTED-DAG: error: random enum domain has no declaration inventory
// UNSUPPORTED-DAG: error: solve before cannot order a property before itself
// UNSUPPORTED-DAG: error: constraint expression is outside the total side-effect-free executable boundary: obelisk.sv.expression.assignment
// UNSUPPORTED-DAG: error: constraint expression is outside the total side-effect-free executable boundary: obelisk.sv.expression.unary_op

module {
  obelisk.sv.symbol.definition @s100.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 100 : i64} {
  }
  obelisk.sv.symbol.root @s101.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 101 : i64} {
    obelisk.sv.symbol.compilation_unit @s102 attributes {hierarchical_name = "$unit", node_id = 102 : i64} {
      obelisk.sv.type.class_type @s103.unsupported attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "unsupported", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "unsupported", node_id = 103 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>, this_variable_path = "unsupported::this", this_variable_symbol = @s101.$root::@s102::@s103.unsupported::@s108.this} {
        obelisk.sv.symbol.class_property @s104.value attributes {hierarchical_name = "unsupported::value", name = "value", node_id = 104 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.class_property @s132.enum_value attributes {hierarchical_name = "unsupported::enum_value", name = "enum_value", node_id = 132 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>} {
        }
        obelisk.sv.symbol.constraint_block @s105.rules attributes {hierarchical_name = "unsupported::rules", name = "rules", node_id = 105 : i64, this_variable_path = "unsupported::rules.this", this_variable_symbol = @s101.$root::@s102::@s103.unsupported::@s105.rules::@s106.this} {
          obelisk.sv.constraint.list attributes {item_count = 5 : i64, node_id = 110 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 111 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 112 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
            obelisk.sv.constraint.implication attributes {node_id = 136 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 137 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 138 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 139 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
            }
            obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 113 : i64, solve_count = 1 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 114 : i64, referenced_path = "unsupported::value", referenced_symbol = @s101.$root::@s102::@s103.unsupported::@s104.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 115 : i64, referenced_path = "unsupported::value", referenced_symbol = @s101.$root::@s102::@s103.unsupported::@s104.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 116 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 117 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 118 : i64, referenced_path = "unsupported::value", referenced_symbol = @s101.$root::@s102::@s103.unsupported::@s104.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 119 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 133 : i64} {
              obelisk.sv.expression.unary_op attributes {node_id = 134 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {node_id = 135 : i64, referenced_path = "unsupported::value", referenced_symbol = @s101.$root::@s102::@s103.unsupported::@s104.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s106.this attributes {hierarchical_name = "unsupported::rules.this", is_compiler_generated, is_const, name = "this", node_id = 120 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>} {
          }
        }
        obelisk.sv.symbol.subroutine @s107.pre_randomize attributes {hierarchical_name = "unsupported::pre_randomize", is_pre_post_randomize, name = "pre_randomize", node_id = 121 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 122 : i64} {
          }
        }
        obelisk.sv.symbol.variable @s108.this attributes {hierarchical_name = "unsupported::this", is_compiler_generated, is_const, name = "this", node_id = 123 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s109.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 124 : i64, referenced_path = "top", referenced_symbol = @s100.top} {
      obelisk.sv.symbol.instance_body @s110.top attributes {hierarchical_name = "top", name = "top", node_id = 125 : i64} {
        obelisk.sv.symbol.variable @s111.object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 126 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 127 : i64, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s112 attributes {hierarchical_name = "top", node_id = 128 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 129 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 130 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s101.$root::@s109.top::@s110.top} {
              obelisk.sv.expression.named_value attributes {node_id = 131 : i64, referenced_path = "top.object", referenced_symbol = @s101.$root::@s109.top::@s110.top::@s111.object, semantic_type = !obelisk.class_handle<@s101.$root::@s102::@s103.unsupported>} {
              }
            }
          }
        }
      }
    }
  }
}
