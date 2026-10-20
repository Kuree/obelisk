// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off require-bytecode=true' --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2023 18.5.4: the leaf elements of a random queue are members of
// a uniqueness group. Two distinct bits with the first set leave one solution
// in both tiers.
//
// class C;
//   rand bit q[$];
//   constraint k { q.size() == 2; unique {q}; q[0] == 1; }
// endclass
// module top;
//   C c = new;
//   initial $display("%0d %p", c.randomize(), c.q);
// endmodule

// CHECK: 1 '{1, 0}

module attributes {coverage.language_version = 2023 : i32, llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s23.this} {
        obelisk.sv.symbol.class_property @s4.q attributes {hierarchical_name = "C::q", name = "q", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.queue<!obelisk.integral<1, false, false, 0 : 0, bit>, 0>} {
        }
        obelisk.sv.symbol.constraint_block @s5.k attributes {hierarchical_name = "C::k", name = "k", node_id = 5 : i64, this_variable_path = "C::k.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.k::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 3 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 8 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.$unit", system_scope_path = "C::k", system_scope_symbol = @s1.$root::@s2::@s3.C::@s5.k} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "C::q", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.q, semantic_type = !obelisk.queue<!obelisk.integral<1, false, false, 0 : 0, bit>, 0>} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.constraint.uniqueness attributes {item_count = 1 : i64, node_id = 12 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "C::q", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.q, semantic_type = !obelisk.queue<!obelisk.integral<1, false, false, 0 : 0, bit>, 0>} {
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 14 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 15 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "C::q", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.q, semantic_type = !obelisk.queue<!obelisk.integral<1, false, false, 0 : 0, bit>, 0>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.expression.conversion attributes {folded_constant = "32'd1", is_implicit = true, is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s6.this attributes {hierarchical_name = "C::k.this", is_compiler_generated, is_const, name = "this", node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.subroutine @s7.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
        }
        obelisk.sv.symbol.variable @s23.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 55 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s19.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 43 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s20.top attributes {hierarchical_name = "top", name = "top", node_id = 44 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s21.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 45 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 46 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s22 attributes {hierarchical_name = "top", node_id = 47 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 48 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 49 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
              obelisk.sv.expression.string_literal attributes {constant_value = "%0d %p", is_signed = false, node_id = 50 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 52 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
              obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "q", node_id = 53 : i64, referenced_path = "C::q", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.q, semantic_type = !obelisk.queue<!obelisk.integral<1, false, false, 0 : 0, bit>, 0>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 54 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
