// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=1 | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode --seed=1 | FileCheck %s

// IEEE 1800-2017 18.5.11 requires one constraint_mode() state shared by
// every instance of a static constraint. Section 18.9 requires constraints
// to start active and to be ignored while disabled. Exercise named and
// class-wide mode changes, then prove randomization observes both states.
// CHECK: static-mode 1 1 1 1 1
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 8 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s31.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::range_c", is_static, name = "range_c", node_id = 5 : i64, sym_name = "s5.range_c"} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.inside attributes {is_signed = false, item_count = 1 : i64, node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.value_range attributes {is_signed = false, node_id = 11 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void} {
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "10", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "15", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::positive_c", name = "positive_c", node_id = 16 : i64, sym_name = "s6.positive_c", this_variable_path = "C::positive_c.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.positive_c::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 17 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 18 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 19 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 22 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::positive_c.this", is_compiler_generated, is_const, name = "this", node_id = 24 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s7.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s8.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 27 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 28 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 29 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 31 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s11.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 32 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 33 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s12.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 34 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 35 : i64, semantic_type = !obelisk.string, sym_name = "s13.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 36 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s14.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 37 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 39 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s16.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 40 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 41 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s17.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s18.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 44 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s19.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 162 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s31.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 45 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s20.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 46 : i64, sym_name = "s21.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 47 : i64, sym_name = "s22"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 48 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s23.a"} {
            obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 49 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 50 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s24.b"} {
            obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 51 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.initial_on", lifetime = 1 : i32, name = "initial_on", node_id = 52 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s25.initial_on"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.named_shared", lifetime = 1 : i32, name = "named_shared", node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s26.named_shared"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.class_shared", lifetime = 1 : i32, name = "class_shared", node_id = 54 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s27.class_shared"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.disabled_ok", lifetime = 1 : i32, name = "disabled_ok", node_id = 55 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s28.disabled_ok"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.enabled_rejects", lifetime = 1 : i32, name = "enabled_rejects", node_id = 56 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s29.enabled_rejects"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 57 : i64, procedure_kind = 0 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 58 : i64} {
            obelisk.sv.statement.list attributes {node_id = 59 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 60 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s23.a} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 61 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s24.b} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 62 : i64, referenced_path = "top.initial_on", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s25.initial_on} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 63 : i64, referenced_path = "top.named_shared", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s26.named_shared} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 64 : i64, referenced_path = "top.class_shared", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s27.class_shared} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 65 : i64, referenced_path = "top.disabled_ok", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s28.disabled_ok} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 66 : i64, referenced_path = "top.enabled_rejects", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s29.enabled_rejects} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 67 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 68 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 69 : i64, referenced_path = "top.initial_on", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s25.initial_on, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 70 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 71 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 72 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 73 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                          obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = false, node_id = 74 : i64, packed_offset = 0 : i64, referenced_path = "C::range_c", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.range_c, semantic_type = !obelisk.void} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 75 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s23.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                            }
                          }
                        }
                        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 76 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                          obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = false, node_id = 77 : i64, packed_offset = 0 : i64, referenced_path = "C::range_c", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.range_c, semantic_type = !obelisk.void} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 78 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s24.b, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 79 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 80 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = false, node_id = 81 : i64, packed_offset = 0 : i64, referenced_path = "C::range_c", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.range_c, semantic_type = !obelisk.void} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 82 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s23.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 83 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 84 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 85 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 86 : i64, referenced_path = "top.named_shared", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s26.named_shared, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 87 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 88 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 89 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                        obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 90 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 91 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                            obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = false, node_id = 92 : i64, packed_offset = 0 : i64, referenced_path = "C::range_c", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.range_c, semantic_type = !obelisk.void} {
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 93 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s23.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                              }
                            }
                          }
                        }
                        obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 94 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 95 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                            obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = false, node_id = 96 : i64, packed_offset = 0 : i64, referenced_path = "C::range_c", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.range_c, semantic_type = !obelisk.void} {
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 97 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s24.b, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 98 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 99 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = false, node_id = 100 : i64, packed_offset = 0 : i64, referenced_path = "C::range_c", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.range_c, semantic_type = !obelisk.void} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 101 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s24.b, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 102 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 103 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 104 : i64, referenced_path = "C::constraint_mode", referenced_symbol = @s1.$root::@s2::@s3.C::@s18.constraint_mode, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 105 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s23.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 106 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 107 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 108 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 109 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 110 : i64, referenced_path = "top.class_shared", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s27.class_shared, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 111 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 112 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 113 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                        obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 114 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 115 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                            obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = false, node_id = 116 : i64, packed_offset = 0 : i64, referenced_path = "C::range_c", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.range_c, semantic_type = !obelisk.void} {
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 117 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s23.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                              }
                            }
                          }
                        }
                        obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 118 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 119 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                            obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = false, node_id = 120 : i64, packed_offset = 0 : i64, referenced_path = "C::range_c", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.range_c, semantic_type = !obelisk.void} {
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 121 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s24.b, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 122 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 123 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 124 : i64, referenced_path = "top.disabled_ok", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s28.disabled_ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 125 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                    obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 126 : i64} {
                      obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 127 : i64} {
                        obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 128 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 129 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 130 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                            }
                          }
                          obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 131 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                            obelisk.sv.expression.integer_literal attributes {constant_value = "20", is_declared_unsized = true, is_signed = true, node_id = 132 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 133 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s24.b, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 134 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "constraint_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 135 : i64, referenced_path = "C::constraint_mode", referenced_symbol = @s1.$root::@s2::@s3.C::@s18.constraint_mode, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 136 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s23.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 137 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 138 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 139 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 140 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 141 : i64, referenced_path = "top.enabled_rejects", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s29.enabled_rejects, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 142 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 143 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 144 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 145 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 146 : i64} {
                            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 147 : i64} {
                              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 148 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                                obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 149 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 150 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                                  }
                                }
                                obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 151 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                                  obelisk.sv.expression.integer_literal attributes {constant_value = "20", is_declared_unsized = true, is_signed = true, node_id = 152 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                                  }
                                }
                              }
                            }
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 153 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s24.b, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                          }
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 154 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 6 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 155 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top::@s22} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "static-mode %0d %0d %0d %0d %0d", is_signed = false, node_id = 156 : i64, semantic_type = !obelisk.ranged_packed_array<247 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 157 : i64, referenced_path = "top.initial_on", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s25.initial_on, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 158 : i64, referenced_path = "top.named_shared", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s26.named_shared, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 159 : i64, referenced_path = "top.class_shared", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s27.class_shared, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 160 : i64, referenced_path = "top.disabled_ok", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s28.disabled_ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 161 : i64, referenced_path = "top.enabled_rejects", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22::@s29.enabled_rejects, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
