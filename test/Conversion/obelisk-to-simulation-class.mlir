// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "supported_class_use", name = "supported_class_use", node_id = 0 : i64, sym_name = "s0.supported_class_use"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "stream_interface", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = true, is_uninstantiated = false, name = "stream_interface", node_id = 1000 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.stream_interface>, sym_name = "s30.stream_interface", this_variable_path = "stream_interface::this", this_variable_symbol = @s1.$root::@s2::@s30.stream_interface::@s31.this} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "stream_interface::this", is_compiler_generated, is_const, name = "this", node_id = 1001 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.stream_interface>, sym_name = "s31.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s30.stream_interface>], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "stream_child", implemented_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s30.stream_interface>], is_abstract = false, is_final = false, is_interface = true, is_uninstantiated = false, name = "stream_child", node_id = 1004 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s33.stream_child>, sym_name = "s33.stream_child", this_variable_path = "stream_child::this", this_variable_symbol = @s1.$root::@s2::@s33.stream_child::@s34.this} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "stream_child::this", is_compiler_generated, is_const, name = "this", node_id = 1005 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s33.stream_child>, sym_name = "s34.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s33.stream_child>], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "supported_object", implemented_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s33.stream_child>, !obelisk.class_handle<@s1.$root::@s2::@s30.stream_interface>], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "supported_object", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>, sym_name = "s3.supported_object", this_variable_path = "supported_object::this", this_variable_symbol = @s1.$root::@s2::@s3.supported_object::@s22.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "supported_object::field", name = "field", node_id = 4 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.field"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "supported_object::bits", name = "bits", node_id = 1100 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s36.bits"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "supported_object::trigger", name = "trigger", node_id = 1006 : i64, semantic_type = !obelisk.event, sym_name = "s35.trigger"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "supported_object::static_field", lifetime = 1 : i32, name = "static_field", node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s23.static_field"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::touch", is_static, name = "touch", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s24.touch", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::touch.value", name = "value", node_id = 52 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s25.value"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 5 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s5.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::pre_randomize", is_builtin, name = "pre_randomize", node_id = 7 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s6.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 8 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::post_randomize", is_builtin, name = "post_randomize", node_id = 9 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s7.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 10 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::get_randstate", is_builtin, name = "get_randstate", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s8.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::set_randstate", is_builtin, name = "set_randstate", node_id = 13 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::set_randstate.state", name = "state", node_id = 15 : i64, semantic_type = !obelisk.string, sym_name = "s10.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::srandom", is_builtin, name = "srandom", node_id = 16 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::srandom.seed", name = "seed", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::rand_mode", is_builtin, name = "rand_mode", node_id = 19 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::rand_mode.on_ff", name = "on_ff", node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s14.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::constraint_mode", is_builtin, name = "constraint_mode", node_id = 22 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 23 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::constraint_mode.on_ff", name = "on_ff", node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_object::this", is_compiler_generated, is_const, name = "this", node_id = 39 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>, sym_name = "s22.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "supported_class_use", is_uninstantiated = false, name = "supported_class_use", node_id = 25 : i64, referenced_path = "supported_class_use", referenced_symbol = @s0.supported_class_use, sym_name = "s17.supported_class_use"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "supported_class_use", name = "supported_class_use", node_id = 26 : i64, sym_name = "s18.supported_class_use"} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "supported_class_use", node_id = 27 : i64, sym_name = "s19"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_class_use.object", name = "object", node_id = 28 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>, sym_name = "s20.object"} {
            obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 29 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_class_use.stream", name = "stream", node_id = 1002 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.stream_interface>, sym_name = "s32.stream"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "supported_class_use", node_id = 30 : i64, procedure_kind = 0 : i32, sym_name = "s21", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 31 : i64} {
            obelisk.sv.statement.list attributes {node_id = 32 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 33 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 1003 : i64, referenced_path = "supported_class_use.stream", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s32.stream} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 1103 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 1104 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 64 : i64, node_id = 1105 : i64, packed_offset = 0 : i64, referenced_path = "supported_object::field", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s4.field, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 1110 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                    }
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 1106 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.member_access attributes {field_ordinal = 64 : i64, node_id = 1107 : i64, packed_offset = 0 : i64, referenced_path = "supported_object::bits", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s36.bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.named_value attributes {node_id = 1108 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 1109 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 1111 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 1112 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 64 : i64, node_id = 1113 : i64, packed_offset = 0 : i64, referenced_path = "supported_object::field", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s4.field, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 1114 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                    }
                  }
                  obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 1115 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<5 : 2 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.member_access attributes {field_ordinal = 64 : i64, node_id = 1116 : i64, packed_offset = 0 : i64, referenced_path = "supported_object::bits", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s36.bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.named_value attributes {node_id = 1117 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 1118 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 1119 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 64 : i64, node_id = 36 : i64, packed_offset = 0 : i64, referenced_path = "supported_object::field", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s4.field, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "42", node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 44 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 64 : i64, is_signed = true, node_id = 46 : i64, packed_offset = 0 : i64, referenced_path = "supported_object::static_field", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s23.static_field, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 48 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 49 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "touch", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 50 : i64, referenced_path = "supported_object::touch", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s24.touch, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "13", is_declared_unsized = true, is_signed = true, node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "srandom", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 55 : i64, referenced_path = "supported_object::srandom", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s11.srandom, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 56 : i64, referenced_path = "supported_class_use.stream", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s32.stream, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.stream_interface>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "123", is_declared_unsized = true, is_signed = true, node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 58 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "set_randstate", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 59 : i64, referenced_path = "supported_object::set_randstate", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s9.set_randstate, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 60 : i64, referenced_path = "supported_class_use.stream", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s32.stream, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.stream_interface>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "get_randstate", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 61 : i64, referenced_path = "supported_object::get_randstate", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s8.get_randstate, semantic_type = !obelisk.string, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_path = "supported_class_use.stream", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s32.stream, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.stream_interface>} {
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

// CHECK: obelisk_sim.class.decl {{.*}}debug_name = "stream_interface"
// CHECK: obelisk_sim.class.decl @{{[^ ]*supported_object}} {{.*}}debug_name = "supported_object"
// CHECK: obelisk_sim.class.field @[[BITS_FIELD:[^ ]+]] {{.*}} : !obelisk_sim.packed_array<7 : 0 x i1> {{.*}}debug_name = "bits"
// CHECK: obelisk_sim.class.field {{.*}} : !obelisk_sim.event {{.*}}debug_name = "trigger"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_state"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_increment"
// CHECK: obelisk_sim.storage.decl {{.*}}hierarchy "supported_object::static_field"
// CHECK: obelisk_sim.class.alloc
// CHECK-NEXT: {{.*}} = obelisk_sim.random.next
// CHECK-NEXT: {{.*}} = obelisk_sim.random.next
// CHECK: %[[BITS_REF:.*]] = obelisk_sim.class.field_ref {{.*}}[@[[BITS_FIELD]]]
// CHECK-NEXT: %[[BITS:.*]] = obelisk_sim.managed.load %[[BITS_REF]]
// CHECK-NEXT: %[[SELECTED:.*]] = obelisk_sim.aggregate.extract %[[BITS]][4]
// CHECK: %[[RANGE_BASE:.*]] = obelisk_sim.managed.load %[[BITS_REF]]
// CHECK-NEXT: %[[FLAT_BITS:.*]] = obelisk_sim.packed.flatten %[[RANGE_BASE]]
// CHECK-NEXT: %[[SHIFTED_BITS:.*]] = arith.shrui %[[FLAT_BITS]], {{.*}}
// CHECK-NEXT: {{.*}} = arith.trunci %[[SHIFTED_BITS]] : i8 to i4
// CHECK: obelisk_sim.ref.store
// CHECK: obelisk_sim.call @{{unit_[0-9]+}}({{.*}}) : (!obelisk_sim.context, i32) -> ()
// The common interface receiver lets canonicalization share one dominating
// dynamic-class guard across all three stream operations.
// CHECK: obelisk_sim.class.is_instance {{.*}} is @{{.*}}supported_object
// CHECK: obelisk_sim.class.cast {{.*}} to !obelisk_sim.class_handle<@{{.*}}supported_object>
// CHECK: arith.constant {{.*}} -8545228632546703407 : i64
// CHECK: obelisk_sim.managed.store
// CHECK: arith.constant {{.*}} 1442695040888963407 : i64
// CHECK: obelisk_sim.managed.store
// CHECK: obelisk_sim.string.format_integer
// CHECK: obelisk_sim.string.literal ":"
// CHECK: obelisk_sim.string.concat
// CHECK: obelisk_sim.string.format_integer
// CHECK: obelisk_sim.string.concat
// CHECK-COUNT-2: obelisk_sim.string.scan_field
// CHECK-COUNT-2: obelisk_sim.string.parse_integer
// CHECK-COUNT-2: obelisk_sim.managed.store
// CHECK: {{.*}} = obelisk_sim.event.create
// CHECK: obelisk_sim.class.field_ref {{.*}} : {{.*}} -> !obelisk_sim.managed_ref<!obelisk_sim.event
// CHECK: obelisk_sim.managed.store {{.*}} : !obelisk_sim.event
// CHECK-NOT: obelisk.sv.
