// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=1 | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode --seed=1 | FileCheck %s

// IEEE 1800-2017 18.5.7 makes this if constraint bidirectional, and 18.6.3
// permits randomize() to fail only when the active constraints are infeasible.
// The i == 0 half of the domain is always legal, even when d == 0.
// CHECK: conditional 1 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "conditional_runtime", name = "conditional_runtime", node_id = 0 : i64, sym_name = "s0.conditional_runtime"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 65 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "conditional_object", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "conditional_object", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.conditional_object>, sym_name = "s3.conditional_object", this_variable_path = "conditional_object::this", this_variable_symbol = @s1.$root::@s2::@s3.conditional_object::@s27.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "conditional_object::d", name = "d", node_id = 4 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.d"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "conditional_object::y", name = "y", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s5.y"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "conditional_object::i", name = "i", node_id = 6 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s6.i"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "conditional_object::rule", name = "rule", node_id = 7 : i64, sym_name = "s7.rule", this_variable_path = "conditional_object::rule.this", this_variable_symbol = @s1.$root::@s2::@s3.conditional_object::@s7.rule::@s8.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 8 : i64} {
            obelisk.sv.constraint.conditional attributes {has_else = false, node_id = 9 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "conditional_object::i", referenced_symbol = @s1.$root::@s2::@s3.conditional_object::@s6.i, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 11 : i64} {
                obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 13 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 14 : i64, referenced_path = "conditional_object::d", referenced_symbol = @s1.$root::@s2::@s3.conditional_object::@s4.d, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 16 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "conditional_object::y", referenced_symbol = @s1.$root::@s2::@s3.conditional_object::@s5.y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_object::rule.this", is_compiler_generated, is_const, name = "this", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.conditional_object>, sym_name = "s8.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "conditional_object::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s9.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "conditional_object::pre_randomize", is_builtin, name = "pre_randomize", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "conditional_object::post_randomize", is_builtin, name = "post_randomize", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "conditional_object::get_randstate", is_builtin, name = "get_randstate", node_id = 27 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s12.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 28 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "conditional_object::set_randstate", is_builtin, name = "set_randstate", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "conditional_object::set_randstate.state", name = "state", node_id = 31 : i64, semantic_type = !obelisk.string, sym_name = "s14.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "conditional_object::srandom", is_builtin, name = "srandom", node_id = 32 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 33 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "conditional_object::srandom.seed", name = "seed", node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s16.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "conditional_object::rand_mode", is_builtin, name = "rand_mode", node_id = 35 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 36 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "conditional_object::rand_mode.on_ff", name = "on_ff", node_id = 37 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "conditional_object::constraint_mode", is_builtin, name = "constraint_mode", node_id = 38 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s19.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 39 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "conditional_object::constraint_mode.on_ff", name = "on_ff", node_id = 40 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s20.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_object::this", is_compiler_generated, is_const, name = "this", node_id = 96 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.conditional_object>, sym_name = "s27.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "conditional_runtime", is_uninstantiated = false, name = "conditional_runtime", node_id = 41 : i64, referenced_path = "conditional_runtime", referenced_symbol = @s0.conditional_runtime, sym_name = "s21.conditional_runtime"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "conditional_runtime", name = "conditional_runtime", node_id = 42 : i64, sym_name = "s22.conditional_runtime", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_runtime.object", lifetime = 1 : i32, name = "object", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.conditional_object>, sym_name = "s23.object"} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 44 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.conditional_object>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_runtime.ok", lifetime = 1 : i32, name = "ok", node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s24.ok"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_runtime.varied", lifetime = 1 : i32, name = "varied", node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s25.varied"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 48 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "conditional_runtime", node_id = 49 : i64, procedure_kind = 0 : i32, sym_name = "s26", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 50 : i64} {
            obelisk.sv.statement.list attributes {node_id = 51 : i64} {
              obelisk.sv.statement.repeat_loop attributes {node_id = 52 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "128", is_declared_unsized = true, is_signed = true, node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.statement.block attributes {node_id = 54 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 55 : i64} {
                    obelisk.sv.statement.expression_statement attributes {node_id = 56 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 57 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 58 : i64, referenced_path = "conditional_runtime.ok", referenced_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime::@s24.ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 59 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          obelisk.sv.expression.l_value_reference attributes {is_signed = true, node_id = 60 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.conditional_runtime", system_scope_path = "conditional_runtime", system_scope_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 62 : i64, referenced_path = "conditional_runtime.object", referenced_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime::@s23.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.conditional_object>} {
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.statement.expression_statement attributes {node_id = 63 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 64 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 65 : i64, referenced_path = "conditional_runtime.ok", referenced_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime::@s24.ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 66 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 67 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                            obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 68 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                              obelisk.sv.expression.l_value_reference attributes {is_signed = true, node_id = 69 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                              }
                            }
                            obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 70 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 71 : i64, operator_kind = 20 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                                obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 72 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                                  obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = false, node_id = 73 : i64, packed_offset = 4294967296 : i64, referenced_path = "conditional_object::i", referenced_symbol = @s1.$root::@s2::@s3.conditional_object::@s6.i, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 74 : i64, referenced_path = "conditional_runtime.object", referenced_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime::@s23.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.conditional_object>} {
                                    }
                                  }
                                }
                                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 75 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                                  obelisk.sv.expression.member_access attributes {field_ordinal = 63 : i64, is_signed = true, node_id = 76 : i64, packed_offset = 4294967296 : i64, referenced_path = "conditional_object::y", referenced_symbol = @s1.$root::@s2::@s3.conditional_object::@s5.y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 77 : i64, referenced_path = "conditional_runtime.object", referenced_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime::@s23.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.conditional_object>} {
                                    }
                                  }
                                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 78 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.statement.expression_statement attributes {node_id = 79 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 80 : i64, operator_kind = 6 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 81 : i64, referenced_path = "conditional_runtime.varied", referenced_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime::@s25.varied, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 82 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 83 : i64, operator_kind = 6 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                            obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 84 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                              obelisk.sv.expression.l_value_reference attributes {is_signed = true, node_id = 85 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                              }
                            }
                            obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 86 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 87 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                                obelisk.sv.expression.member_access attributes {field_ordinal = 63 : i64, is_signed = true, node_id = 88 : i64, packed_offset = 4294967296 : i64, referenced_path = "conditional_object::y", referenced_symbol = @s1.$root::@s2::@s3.conditional_object::@s5.y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 89 : i64, referenced_path = "conditional_runtime.object", referenced_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime::@s23.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.conditional_object>} {
                                  }
                                }
                                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 90 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
              obelisk.sv.statement.expression_statement attributes {node_id = 91 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 92 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.conditional_runtime", system_scope_path = "conditional_runtime", system_scope_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "conditional %0d %0d", is_signed = false, node_id = 93 : i64, semantic_type = !obelisk.ranged_packed_array<151 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 94 : i64, referenced_path = "conditional_runtime.ok", referenced_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime::@s24.ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 95 : i64, referenced_path = "conditional_runtime.varied", referenced_symbol = @s1.$root::@s21.conditional_runtime::@s22.conditional_runtime::@s25.varied, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
