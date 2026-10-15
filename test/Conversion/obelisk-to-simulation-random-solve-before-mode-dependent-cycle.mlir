// RUN: %split-file %s %t
// RUN: obelisk-opt %t/accept.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t/reject.mlir '--lower-obelisk-to-sim=opt-level=0' --verify-diagnostics -o /dev/null
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.9: randomize() considers only the active constraint
// blocks. C's cycle spans a, which is always enabled, and b, which a named
// constraint_mode() call disables. D's cycle spans its inline constraints and
// a, which a class-wide call disables. Both calls lower to the runtime solver,
// which orders the active edges and reports an active cycle. A cycle among
// blocks that no constraint_mode() call can disable is a compile-time error.
//
// accept.sv:
//   class C;
//     rand bit x, y;
//     constraint a { solve x before y; }
//     constraint b { solve y before x; }
//   endclass
//   class D;
//     rand bit x, y;
//     constraint a { solve x before y; }
//   endclass
//   module top;
//     C c = new;
//     D d = new;
//     initial begin
//       c.b.constraint_mode(0);
//       void'(c.randomize());
//       d.constraint_mode(0);
//       void'(d.randomize() with { solve y before x; });
//     end
//   endmodule
//
// reject.sv:
//   class C;
//     rand bit x, y;
//     constraint a { solve x before y; }
//     constraint b { solve y before x; }
//   endclass
//   module top;
//     C c = new;
//     initial void'(c.randomize());
//   endmodule

// CHECK-LABEL: simulation.func private @unit_2
// CHECK: simulation.random.solve
// CHECK: __obelisk_class_s22_D_field___obelisk_constraint_mode
// CHECK: simulation.random.solve

//--- accept.mlir
module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s44.this} {
        obelisk.sv.symbol.class_property @s4.x attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.class_property @s5.y attributes {hierarchical_name = "C::y", name = "y", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.constraint_block @s6.a attributes {hierarchical_name = "C::a", name = "a", node_id = 6 : i64, this_variable_path = "C::a.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.a::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 8 : i64, solve_count = 1 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.variable @s7.this attributes {hierarchical_name = "C::a.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.constraint_block @s8.b attributes {hierarchical_name = "C::b", name = "b", node_id = 12 : i64, this_variable_path = "C::b.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s8.b::@s9.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 13 : i64} {
            obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 14 : i64, solve_count = 1 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.variable @s9.this attributes {hierarchical_name = "C::b.this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.subroutine @s10.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.variable @s44.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 96 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
      obelisk.sv.type.class_type @s22.D attributes {bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "D", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "D", node_id = 38 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.D>, this_variable_path = "D::this", this_variable_symbol = @s1.$root::@s2::@s22.D::@s45.this} {
        obelisk.sv.symbol.class_property @s23.x attributes {hierarchical_name = "D::x", name = "x", node_id = 39 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.class_property @s24.y attributes {hierarchical_name = "D::y", name = "y", node_id = 40 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.constraint_block @s25.a attributes {hierarchical_name = "D::a", name = "a", node_id = 41 : i64, this_variable_path = "D::a.this", this_variable_symbol = @s1.$root::@s2::@s22.D::@s25.a::@s26.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 42 : i64} {
            obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 43 : i64, solve_count = 1 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "D::x", referenced_symbol = @s1.$root::@s2::@s22.D::@s23.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "D::y", referenced_symbol = @s1.$root::@s2::@s22.D::@s24.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.variable @s26.this attributes {hierarchical_name = "D::a.this", is_compiler_generated, is_const, name = "this", node_id = 46 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.D>} {
          }
        }
        obelisk.sv.symbol.subroutine @s27.randomize attributes {hierarchical_name = "D::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 47 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 48 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s37.constraint_mode attributes {hierarchical_name = "D::constraint_mode", is_builtin, name = "constraint_mode", node_id = 64 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 65 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s38.on_ff attributes {direction = 0 : i32, hierarchical_name = "D::constraint_mode.on_ff", name = "on_ff", node_id = 66 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s45.this attributes {hierarchical_name = "D::this", is_compiler_generated, is_const, name = "this", node_id = 97 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.D>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s39.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 67 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s40.top attributes {hierarchical_name = "top", name = "top", node_id = 68 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s41.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 69 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 70 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable @s42.d attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 71 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.D>} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 72 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.D>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s43 attributes {hierarchical_name = "top", node_id = 73 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 74 : i64} {
            obelisk.sv.statement.list attributes {node_id = 75 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 76 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 77 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s39.top::@s40.top} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "b", node_id = 78 : i64, referenced_path = "C::b", referenced_symbol = @s1.$root::@s2::@s3.C::@s8.b, semantic_type = !obelisk.void} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 79 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s39.top::@s40.top::@s41.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 80 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 81 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 82 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s39.top::@s40.top} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 83 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s39.top::@s40.top::@s41.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 84 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 85 : i64, referenced_path = "D::constraint_mode", referenced_symbol = @s1.$root::@s2::@s22.D::@s37.constraint_mode, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 86 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s39.top::@s40.top::@s42.d, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.D>} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 87 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 88 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 89 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 90 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s39.top::@s40.top} {
                  obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 91 : i64} {
                    obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 92 : i64, solve_count = 1 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 93 : i64, referenced_path = "D::y", referenced_symbol = @s1.$root::@s2::@s22.D::@s24.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 94 : i64, referenced_path = "D::x", referenced_symbol = @s1.$root::@s2::@s22.D::@s23.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      }
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s39.top::@s40.top::@s42.d, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.D>} {
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


//--- reject.mlir
module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s26.this} {
        obelisk.sv.symbol.class_property @s4.x attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.class_property @s5.y attributes {hierarchical_name = "C::y", name = "y", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.constraint_block @s6.a attributes {hierarchical_name = "C::a", name = "a", node_id = 6 : i64, this_variable_path = "C::a.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.a::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 8 : i64, solve_count = 1 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.variable @s7.this attributes {hierarchical_name = "C::a.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.constraint_block @s8.b attributes {hierarchical_name = "C::b", name = "b", node_id = 12 : i64, this_variable_path = "C::b.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s8.b::@s9.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 13 : i64} {
            obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 14 : i64, solve_count = 1 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.variable @s9.this attributes {hierarchical_name = "C::b.this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.subroutine @s10.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.variable @s26.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 46 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s22.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 38 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s23.top attributes {hierarchical_name = "top", name = "top", node_id = 39 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s24.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 40 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 41 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s25 attributes {hierarchical_name = "top", node_id = 42 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 43 : i64} {
            // expected-error @+1 {{solve before ordering contains a cycle}}
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s22.top::@s23.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s22.top::@s23.top::@s24.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

