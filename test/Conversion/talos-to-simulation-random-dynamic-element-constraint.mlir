// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' --verify-diagnostics

// IEEE 1800-2023 18.5.7.1 solves the size of a random dynamic array or queue
// before its element constraints, which read a fixed shadow of its leading
// elements. Without an always-active constant bound on size() the shadow has
// no size, so the element constraint is reported as a boundary.
//
// class C; rand bit a[]; constraint k { a[0] == 1; } endclass
// module top; C c; initial begin c = new; void'(c.randomize()); end endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      talos.sv.type.class_type @s3.C attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s23.this} {
        talos.sv.symbol.class_property @s4.a attributes {hierarchical_name = "C::a", name = "a", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !talos.dynarray<!talos.integral<1, false, false, 0 : 0, bit>>} {
        }
        talos.sv.symbol.constraint_block @s5.k attributes {hierarchical_name = "C::k", name = "k", node_id = 5 : i64, this_variable_path = "C::k.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.k::@s6.this} {
          talos.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            talos.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              talos.sv.expression.binary_op attributes {is_signed = false, node_id = 8 : i64, operator_kind = 9 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                talos.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 9 : i64, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  talos.sv.expression.element_select attributes {is_signed = false, node_id = 10 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    // expected-error @+1 {{constraints on the elements of a random dynamic array or queue require an always-active hard constraint that bounds its size() by a constant}}
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !talos.dynarray<!talos.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                talos.sv.expression.conversion attributes {folded_constant = "32'd1", is_implicit = true, is_signed = false, node_id = 13 : i64, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          talos.sv.symbol.variable @s6.this attributes {hierarchical_name = "C::k.this", is_compiler_generated, is_const, name = "this", node_id = 15 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        talos.sv.symbol.subroutine @s7.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 16 : i64, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        talos.sv.symbol.variable @s23.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 49 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    talos.sv.symbol.instance @s19.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 36 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s20.top attributes {hierarchical_name = "top", name = "top", node_id = 37 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s21.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 38 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
        }
        talos.sv.symbol.procedural_block @s22 attributes {hierarchical_name = "top", node_id = 39 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 40 : i64} {
            talos.sv.statement.list attributes {node_id = 41 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 42 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 43 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                  }
                  talos.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 45 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 46 : i64} {
                talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 47 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
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
