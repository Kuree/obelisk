// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off require-bytecode=true' --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s --check-prefix=PLAN

// IEEE 1800-2023 18.5.7.1: foreach iterates over the elements below the
// solved size, and only those reads are valid. Reading a[1] needs size 2, the
// guarded read a[i + 1] constrains no last element, and '{0, 1} is the only
// solution in both tiers. The elements are a shadow solved after the size,
// which replaces the random element fill.
//
// class C;
//   rand bit a[];
//   constraint k {
//     a.size() <= 2;
//     foreach (a[i]) (i < a.size() - 1) -> a[i + 1] > a[i];
//     a[1] == 1;
//   }
// endclass
// module top;
//   C c = new;
//   initial $display("%0d %p", c.randomize(), c.a);
// endmodule

// CHECK: 1 '{0, 1}

// PLAN: simulation.random_container_properties = []
// PLAN-SAME: simulation.random_properties = [#simulation.random_property<{{.*}}containerSize = <type = !simulation.dynamic_array<i1>
// PLAN-SAME: #simulation.random_property<{{.*}}type = !simulation.unpacked_array<0 : 1 x i1, {{.*}}containerElements = <type = !simulation.dynamic_array<i1>, sizeProperty = 0>>]

module attributes {coverage.language_version = 2023 : i32, llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s24.this} {
        obelisk.sv.symbol.class_property @s4.a attributes {hierarchical_name = "C::a", name = "a", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
        }
        obelisk.sv.symbol.constraint_block @s5.k attributes {hierarchical_name = "C::k", name = "k", node_id = 5 : i64, this_variable_path = "C::k.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.k::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 3 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 8 : i64, operator_kind = 15 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.$unit", system_scope_path = "C::k", system_scope_symbol = @s1.$root::@s2::@s3.C::@s5.k} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.constraint.foreach attributes {loop_dimensions = [{has_iterator = true, has_static_range = false, iterator_path = "C::k.i", iterator_symbol = @s1.$root::@s2::@s3.C::@s5.k::@s6.i, iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>}], node_id = 12 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.constraint.implication attributes {node_id = 14 : i64} {
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 15 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "C::k.i", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.k::@s6.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 17 : i64, operator_kind = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.$unit", system_scope_path = "C::k", system_scope_symbol = @s1.$root::@s2::@s3.C::@s5.k} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 21 : i64} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 22 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                      obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 25 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 26 : i64, referenced_path = "C::k.i", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.k::@s6.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                    obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 30 : i64, referenced_path = "C::k.i", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.k::@s6.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 31 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 32 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 33 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 34 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.expression.conversion attributes {folded_constant = "32'd1", is_implicit = true, is_signed = false, node_id = 37 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s7.this attributes {hierarchical_name = "C::k.this", is_compiler_generated, is_const, name = "this", node_id = 39 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
          obelisk.sv.symbol.iterator @s6.i attributes {array_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>, hierarchical_name = "C::k.i", index_method_name = "", is_const, name = "i", node_id = 72 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s8.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 40 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 41 : i64} {
          }
        }
        obelisk.sv.symbol.variable @s24.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 73 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s20.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 60 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s21.top attributes {hierarchical_name = "top", name = "top", node_id = 61 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s22.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 62 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 63 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s23 attributes {hierarchical_name = "top", node_id = 64 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 65 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 66 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top} {
              obelisk.sv.expression.string_literal attributes {constant_value = "%0d %p", is_signed = false, node_id = 67 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 68 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 69 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
              obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "a", node_id = 70 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 71 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
