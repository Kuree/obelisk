// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' '--encode-talos-sim-to-bytecode=vpi=off require-bytecode=true' --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2023 18.4.2: a randc variable cycles through every value of its
// range before repeating one. Four calls on a two-bit randc property visit all
// four values in both tiers; the bytecode sampler carries the cycle's domain
// identity in a string register.
//
// class C;
//   randc bit [1:0] x;
// endclass
// module top;
//   C c = new;
//   int seen;
//   initial begin
//     repeat (4) begin
//       void'(c.randomize());
//       seen |= 1 << c.x;
//     end
//     $display("%b", seen[3:0]);
//   end
// endmodule

// CHECK: 1111

module attributes {coverage.language_version = 2023 : i32, llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      talos.sv.type.class_type @s3.C attributes {bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s22.this} {
        talos.sv.symbol.class_property @s4.x attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 2 : i32, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
        }
        talos.sv.symbol.subroutine @s5.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 5 : i64, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 6 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s6.pre_randomize attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 7 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 8 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s7.post_randomize attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 9 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 10 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s8.get_randstate attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 11 : i64, semantic_type = !talos.subroutine<() -> !talos.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s9.set_randstate attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 13 : i64, semantic_type = !talos.subroutine<(!talos.string) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 14 : i64} {
          }
          talos.sv.symbol.formal_argument @s10.state attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 15 : i64, semantic_type = !talos.string} {
          }
        }
        talos.sv.symbol.subroutine @s11.srandom attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 16 : i64, semantic_type = !talos.subroutine<(!talos.integral<32, true, false, 31 : 0, int>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 17 : i64} {
          }
          talos.sv.symbol.formal_argument @s12.seed attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 18 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
          }
        }
        talos.sv.symbol.subroutine @s13.rand_mode attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 19 : i64, semantic_type = !talos.subroutine<(!talos.integral<1, false, false, 0 : 0, bit>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 20 : i64} {
          }
          talos.sv.symbol.formal_argument @s14.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 21 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        talos.sv.symbol.subroutine @s15.constraint_mode attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 22 : i64, semantic_type = !talos.subroutine<(!talos.integral<1, false, false, 0 : 0, bit>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 23 : i64} {
          }
          talos.sv.symbol.formal_argument @s16.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 24 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        talos.sv.symbol.variable @s22.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 56 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    talos.sv.symbol.instance @s17.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 25 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s18.top attributes {hierarchical_name = "top", name = "top", node_id = 26 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s19.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 27 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
          talos.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 28 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        talos.sv.symbol.variable @s20.seen attributes {hierarchical_name = "top.seen", lifetime = 1 : i32, name = "seen", node_id = 29 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.procedural_block @s21 attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 31 : i64} {
            talos.sv.statement.list attributes {node_id = 32 : i64} {
              talos.sv.statement.repeat_loop attributes {node_id = 33 : i64} {
                talos.sv.expression.integer_literal attributes {constant_value = "4", is_declared_unsized = true, is_signed = true, node_id = 34 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
                talos.sv.statement.block attributes {node_id = 35 : i64} {
                  talos.sv.statement.list attributes {node_id = 36 : i64} {
                    talos.sv.statement.expression_statement attributes {node_id = 37 : i64} {
                      talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 38 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s17.top::@s18.top} {
                        talos.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s17.top::@s18.top::@s19.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                        }
                      }
                    }
                    talos.sv.statement.expression_statement attributes {node_id = 40 : i64} {
                      talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 41 : i64, operator_kind = 6 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        talos.sv.expression.named_value attributes {is_signed = true, node_id = 42 : i64, referenced_path = "top.seen", referenced_symbol = @s1.$root::@s17.top::@s18.top::@s20.seen, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        }
                        talos.sv.expression.binary_op attributes {is_signed = true, node_id = 43 : i64, operator_kind = 6 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                          talos.sv.expression.l_value_reference attributes {is_signed = true, node_id = 44 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                          }
                          talos.sv.expression.binary_op attributes {is_signed = true, node_id = 45 : i64, operator_kind = 23 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                            talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 46 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                            }
                            talos.sv.expression.member_access attributes {is_signed = false, member_name = "x", node_id = 47 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.x, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                              talos.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s17.top::@s18.top::@s19.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 49 : i64} {
                talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 50 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s17.top::@s18.top} {
                  talos.sv.expression.string_literal attributes {constant_value = "%b", is_signed = false, node_id = 51 : i64, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  talos.sv.expression.range_select attributes {is_signed = false, node_id = 52 : i64, selection_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                    talos.sv.expression.named_value attributes {is_signed = true, node_id = 53 : i64, referenced_path = "top.seen", referenced_symbol = @s1.$root::@s17.top::@s18.top::@s20.seen, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                    talos.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 54 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                    talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 55 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
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
