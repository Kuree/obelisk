// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 8.17: `default` in a constructor argument list expands to
// the base constructor's arguments, and a constructor using it without
// super.new gets an implicit super.new(default) that passes them all on. The
// implicit constructor of `extends B(default)` takes and passes them as well.
//
// class B; int value; function new(int n = 1); value = n; endfunction endclass
// class D extends B; function new(default); endfunction endclass
// class E extends B(default); endclass
// module top; D d; E e; initial begin d = new(2); e = new(3); end endmodule
//
// The unused std package classes and built-in randomization methods are
// omitted from the input below.

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: %arg1: !simulation.class_handle<@__talos_class_s{{[0-9]+}}_B>
// CHECK: simulation.managed.store %arg2

// D::new passes its argument, not B::new's default.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: %arg1: !simulation.class_handle<@__talos_class_s{{[0-9]+}}_D>
// CHECK-SAME: %arg2: i32
// CHECK: %[[D_BASE:.+]] = simulation.class.cast %arg1
// CHECK: simulation.class.direct_call @unit_0 %[[D_BASE]](%arg2)

// E's implicit constructor takes and passes B::new's argument.
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: %arg1: !simulation.class_handle<@__talos_class_s{{[0-9]+}}_E>
// CHECK-SAME: %arg2: i32
// CHECK: %[[E_BASE:.+]] = simulation.class.cast %arg1
// CHECK: simulation.class.direct_call @unit_0 %[[E_BASE]](%arg2)

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      talos.sv.type.class_type @s3.B attributes {bitstream_width = 32 : i64, constructor_path = "B::new", constructor_symbol = @s1.$root::@s2::@s3.B::@s5.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "B", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "B", node_id = 3 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.B>, this_variable_path = "B::this", this_variable_symbol = @s1.$root::@s2::@s3.B::@s58.this} {
        talos.sv.symbol.class_property @s4.value attributes {hierarchical_name = "B::value", name = "value", node_id = 4 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.subroutine @s5.new attributes {hierarchical_name = "B::new", is_constructor, name = "new", node_id = 5 : i64, semantic_type = !talos.subroutine<(!talos.integral<32, true, false, 31 : 0, int>) -> !talos.void, false>, subroutine_kind = 0 : i32, this_variable_path = "B::new.this", this_variable_symbol = @s1.$root::@s2::@s3.B::@s5.new::@s7.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 6 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 7 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              talos.sv.expression.named_value attributes {is_signed = true, node_id = 8 : i64, referenced_path = "B::value", referenced_symbol = @s1.$root::@s2::@s3.B::@s4.value, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
              talos.sv.expression.named_value attributes {is_signed = true, node_id = 9 : i64, referenced_path = "B::new.n", referenced_symbol = @s1.$root::@s2::@s3.B::@s5.new::@s6.n, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          talos.sv.symbol.formal_argument @s6.n attributes {direction = 0 : i32, hierarchical_name = "B::new.n", name = "n", node_id = 10 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            }
          }
          talos.sv.symbol.variable @s7.this attributes {hierarchical_name = "B::new.this", is_compiler_generated, is_const, name = "this", node_id = 12 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.B>} {
          }
        }
        talos.sv.symbol.variable @s58.this attributes {hierarchical_name = "B::this", is_compiler_generated, is_const, name = "this", node_id = 105 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.B>} {
        }
      }
      talos.sv.type.class_type @s20.D attributes {base_class = !talos.class_handle<@s1.$root::@s2::@s3.B>, bitstream_width = 32 : i64, constructor_path = "D::new", constructor_symbol = @s1.$root::@s2::@s20.D::@s22.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "D", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "D", node_id = 33 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s20.D>, this_variable_path = "D::this", this_variable_symbol = @s1.$root::@s2::@s20.D::@s59.this} {
        talos.sv.symbol.transparent_member @s21.value attributes {hierarchical_name = "D::value", name = "value", node_id = 34 : i64} {
        }
        talos.sv.symbol.subroutine @s22.new attributes {has_defaulted_super_arg, hierarchical_name = "D::new", is_constructor, name = "new", node_id = 35 : i64, semantic_type = !talos.subroutine<(!talos.integral<32, true, false, 31 : 0, int>) -> !talos.void, false>, subroutine_kind = 0 : i32, this_variable_path = "D::new.this", this_variable_symbol = @s1.$root::@s2::@s20.D::@s22.new::@s24.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 36 : i64} {
          }
          talos.sv.symbol.formal_argument @s23.n attributes {direction = 0 : i32, hierarchical_name = "D::new.n", name = "n", node_id = 37 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 38 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            }
          }
          talos.sv.symbol.variable @s24.this attributes {hierarchical_name = "D::new.this", is_compiler_generated, is_const, name = "this", node_id = 39 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s20.D>} {
          }
        }
        talos.sv.symbol.variable @s59.this attributes {hierarchical_name = "D::this", is_compiler_generated, is_const, name = "this", node_id = 106 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s20.D>} {
        }
      }
      talos.sv.type.class_type @s37.E attributes {base_class = !talos.class_handle<@s1.$root::@s2::@s3.B>, bitstream_width = 32 : i64, constructor_path = "E::new", constructor_symbol = @s1.$root::@s2::@s37.E::@s51.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "E", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "E", node_id = 60 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s37.E>, this_variable_path = "E::this", this_variable_symbol = @s1.$root::@s2::@s37.E::@s60.this} {
        talos.sv.symbol.transparent_member @s38.value attributes {hierarchical_name = "E::value", name = "value", node_id = 61 : i64} {
        }
        talos.sv.symbol.subroutine @s51.new attributes {has_defaulted_super_arg, hierarchical_name = "E::new", is_builtin, is_constructor, name = "new", node_id = 82 : i64, semantic_type = !talos.subroutine<(!talos.integral<32, true, false, 31 : 0, int>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 83 : i64} {
          }
          talos.sv.symbol.formal_argument @s52.n attributes {direction = 0 : i32, hierarchical_name = "E::new.n", name = "n", node_id = 84 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 85 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        talos.sv.symbol.variable @s60.this attributes {hierarchical_name = "E::this", is_compiler_generated, is_const, name = "this", node_id = 107 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s37.E>} {
        }
      }
    }
    talos.sv.symbol.instance @s53.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 86 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s54.top attributes {hierarchical_name = "top", name = "top", node_id = 87 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s55.d attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 88 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s20.D>} {
        }
        talos.sv.symbol.variable @s56.e attributes {hierarchical_name = "top.e", lifetime = 1 : i32, name = "e", node_id = 89 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s37.E>} {
        }
        talos.sv.symbol.procedural_block @s57 attributes {hierarchical_name = "top", node_id = 90 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 91 : i64} {
            talos.sv.statement.list attributes {node_id = 92 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 93 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 94 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s20.D>} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55.d, semantic_type = !talos.class_handle<@s1.$root::@s2::@s20.D>} {
                  }
                  talos.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 96 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s20.D>} {
                    talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 97 : i64, referenced_path = "D::new", referenced_symbol = @s1.$root::@s2::@s20.D::@s22.new, semantic_type = !talos.void, subroutine_kind = 0 : i32} {
                      talos.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 98 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 99 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 100 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s37.E>} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 101 : i64, referenced_path = "top.e", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s56.e, semantic_type = !talos.class_handle<@s1.$root::@s2::@s37.E>} {
                  }
                  talos.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 102 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s37.E>} {
                    talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 103 : i64, referenced_path = "E::new", referenced_symbol = @s1.$root::@s2::@s37.E::@s51.new, semantic_type = !talos.void, subroutine_kind = 0 : i32} {
                      talos.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 104 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
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

