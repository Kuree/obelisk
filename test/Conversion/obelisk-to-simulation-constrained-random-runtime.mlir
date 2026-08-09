// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=1 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.exe --execution-tier=bytecode --seed=1 | FileCheck %s --check-prefix=RUNTIME

// RUNTIME: preferred 1 1
// RUNTIME-NEXT: fallback 1 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "soft_runtime", name = "soft_runtime", node_id = 0 : i64, sym_name = "s0.soft_runtime"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "soft_object", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "soft_object", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>, sym_name = "s3.soft_object", this_variable_path = "soft_object::this", this_variable_symbol = @s1.$root::@s2::@s3.soft_object::@s26.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "soft_object::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "soft_object::legal", name = "legal", node_id = 5 : i64, sym_name = "s5.legal", this_variable_path = "soft_object::legal.this", this_variable_symbol = @s1.$root::@s2::@s3.soft_object::@s5.legal::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.inside attributes {item_count = 1 : i64, node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.conversion attributes {node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "soft_object::value", referenced_symbol = @s1.$root::@s2::@s3.soft_object::@s4.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.value_range attributes {node_id = 11 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void} {
                  obelisk.sv.expression.conversion attributes {node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "soft_object::legal.this", is_compiler_generated, is_const, name = "this", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "soft_object::preferred", name = "preferred", node_id = 17 : i64, sym_name = "s7.preferred", this_variable_path = "soft_object::preferred.this", this_variable_symbol = @s1.$root::@s2::@s3.soft_object::@s7.preferred::@s8.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 18 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 19 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 20 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.conversion attributes {node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "soft_object::value", referenced_symbol = @s1.$root::@s2::@s3.soft_object::@s4.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.conversion attributes {node_id = 23 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "soft_object::preferred.this", is_compiler_generated, is_const, name = "this", node_id = 25 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>, sym_name = "s8.this"} {
          }
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "soft_object::preferred_one", name = "preferred_one", node_id = 84 : i64, sym_name = "s27.preferred_one", this_variable_path = "soft_object::preferred_one.this", this_variable_symbol = @s1.$root::@s2::@s3.soft_object::@s27.preferred_one::@s28.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 85 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 86 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 87 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.conversion attributes {node_id = 88 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 89 : i64, referenced_path = "soft_object::value", referenced_symbol = @s1.$root::@s2::@s3.soft_object::@s4.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.conversion attributes {node_id = 90 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 91 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "soft_object::preferred_one.this", is_compiler_generated, is_const, name = "this", node_id = 92 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>, sym_name = "s28.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "soft_object::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 26 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s9.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "soft_object::pre_randomize", is_builtin, name = "pre_randomize", node_id = 28 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 29 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "soft_object::post_randomize", is_builtin, name = "post_randomize", node_id = 30 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 31 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "soft_object::get_randstate", is_builtin, name = "get_randstate", node_id = 32 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s12.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 33 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "soft_object::set_randstate", is_builtin, name = "set_randstate", node_id = 34 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 35 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "soft_object::set_randstate.state", name = "state", node_id = 36 : i64, semantic_type = !obelisk.string, sym_name = "s14.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "soft_object::srandom", is_builtin, name = "srandom", node_id = 37 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 38 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "soft_object::srandom.seed", name = "seed", node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s16.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "soft_object::rand_mode", is_builtin, name = "rand_mode", node_id = 40 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 41 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "soft_object::rand_mode.on_ff", name = "on_ff", node_id = 42 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "soft_object::constraint_mode", is_builtin, name = "constraint_mode", node_id = 43 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s19.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 44 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "soft_object::constraint_mode.on_ff", name = "on_ff", node_id = 45 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s20.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "soft_object::this", is_compiler_generated, is_const, name = "this", node_id = 83 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>, sym_name = "s26.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "soft_runtime", is_uninstantiated = false, name = "soft_runtime", node_id = 46 : i64, referenced_path = "soft_runtime", referenced_symbol = @s0.soft_runtime, sym_name = "s21.soft_runtime"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "soft_runtime", name = "soft_runtime", node_id = 47 : i64, sym_name = "s22.soft_runtime", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "soft_runtime.object", lifetime = 1 : i32, name = "object", node_id = 48 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>, sym_name = "s23.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 49 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "soft_runtime.result", lifetime = 1 : i32, name = "result", node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s24.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "soft_runtime", node_id = 51 : i64, procedure_kind = 0 : i32, sym_name = "s25", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 52 : i64} {
            obelisk.sv.statement.list attributes {node_id = 53 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 55 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 56 : i64, referenced_path = "soft_runtime.result", referenced_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime::@s24.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.soft_runtime", system_scope_path = "soft_runtime", system_scope_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime} {
                    obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_path = "soft_runtime.object", referenced_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime::@s23.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 59 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 60 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.soft_runtime", system_scope_path = "soft_runtime", system_scope_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "preferred %0d %0d", node_id = 61 : i64, semantic_type = !obelisk.ranged_packed_array<135 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_path = "soft_runtime.result", referenced_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime::@s24.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, node_id = 63 : i64, packed_offset = 4294967296 : i64, referenced_path = "soft_object::value", referenced_symbol = @s1.$root::@s2::@s3.soft_object::@s4.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 64 : i64, referenced_path = "soft_runtime.object", referenced_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime::@s23.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 65 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 66 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 67 : i64, referenced_path = "soft_runtime.result", referenced_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime::@s24.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 68 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.soft_runtime", system_scope_path = "soft_runtime", system_scope_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime} {
                    obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 69 : i64} {
                      obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 70 : i64} {
                        obelisk.sv.expression.binary_op attributes {node_id = 71 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.conversion attributes {node_id = 72 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                            obelisk.sv.expression.named_value attributes {node_id = 73 : i64, referenced_path = "soft_object::value", referenced_symbol = @s1.$root::@s2::@s3.soft_object::@s4.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                            }
                          }
                          obelisk.sv.expression.conversion attributes {node_id = 74 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                            obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 75 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 76 : i64, referenced_path = "soft_runtime.object", referenced_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime::@s23.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 77 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 78 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.soft_runtime", system_scope_path = "soft_runtime", system_scope_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "fallback %0d %0d", node_id = 79 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 80 : i64, referenced_path = "soft_runtime.result", referenced_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime::@s24.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, node_id = 81 : i64, packed_offset = 4294967296 : i64, referenced_path = "soft_object::value", referenced_symbol = @s1.$root::@s2::@s3.soft_object::@s4.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 82 : i64, referenced_path = "soft_runtime.object", referenced_symbol = @s1.$root::@s21.soft_runtime::@s22.soft_runtime::@s23.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.soft_object>} {
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
