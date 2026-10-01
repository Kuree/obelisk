// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.leaf attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "leaf", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "leaf", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, this_variable_path = "leaf::this", this_variable_symbol = @s1.$root::@s2::@s3.leaf::@s54.this} {
        obelisk.sv.symbol.class_property @s4.x attributes {hierarchical_name = "leaf::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.class_property @s5.floor attributes {hierarchical_name = "leaf::floor", name = "floor", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.constraint_block @s6.bounded attributes {hierarchical_name = "leaf::bounded", name = "bounded", node_id = 6 : i64, this_variable_path = "leaf::bounded.this", this_variable_symbol = @s1.$root::@s2::@s3.leaf::@s6.bounded::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 8 : i64} {
              obelisk.sv.expression.inside attributes {is_signed = false, item_count = 1 : i64, node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 10 : i64, referenced_path = "leaf::x", referenced_symbol = @s1.$root::@s2::@s3.leaf::@s4.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.value_range attributes {is_signed = false, node_id = 11 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 14 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 15 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "leaf::x", referenced_symbol = @s1.$root::@s2::@s3.leaf::@s4.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "leaf::floor", referenced_symbol = @s1.$root::@s2::@s3.leaf::@s5.floor, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s7.this attributes {hierarchical_name = "leaf::bounded.this", is_compiler_generated, is_const, name = "this", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>} {
          }
        }
        obelisk.sv.symbol.subroutine @s8.randomize attributes {hierarchical_name = "leaf::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 19 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s9.pre_randomize attributes {hierarchical_name = "leaf::pre_randomize", is_builtin, name = "pre_randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s10.post_randomize attributes {hierarchical_name = "leaf::post_randomize", is_builtin, name = "post_randomize", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s11.get_randstate attributes {hierarchical_name = "leaf::get_randstate", is_builtin, name = "get_randstate", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s12.set_randstate attributes {hierarchical_name = "leaf::set_randstate", is_builtin, name = "set_randstate", node_id = 27 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 28 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s13.state attributes {direction = 0 : i32, hierarchical_name = "leaf::set_randstate.state", name = "state", node_id = 29 : i64, semantic_type = !obelisk.string} {
          }
        }
        obelisk.sv.symbol.subroutine @s14.srandom attributes {hierarchical_name = "leaf::srandom", is_builtin, name = "srandom", node_id = 30 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 31 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s15.seed attributes {direction = 0 : i32, hierarchical_name = "leaf::srandom.seed", name = "seed", node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s16.rand_mode attributes {hierarchical_name = "leaf::rand_mode", is_builtin, name = "rand_mode", node_id = 33 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 34 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s17.on_ff attributes {direction = 0 : i32, hierarchical_name = "leaf::rand_mode.on_ff", name = "on_ff", node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.subroutine @s18.constraint_mode attributes {hierarchical_name = "leaf::constraint_mode", is_builtin, name = "constraint_mode", node_id = 36 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 37 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s19.on_ff attributes {direction = 0 : i32, hierarchical_name = "leaf::constraint_mode.on_ff", name = "on_ff", node_id = 38 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s54.this attributes {hierarchical_name = "leaf::this", is_compiler_generated, is_const, name = "this", node_id = 92 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>} {
        }
      }
      obelisk.sv.type.class_type @s20.middle attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "middle", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "middle", node_id = 39 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.middle>, this_variable_path = "middle::this", this_variable_symbol = @s1.$root::@s2::@s20.middle::@s53.this} {
        obelisk.sv.symbol.class_property @s21.child attributes {hierarchical_name = "middle::child", name = "child", node_id = 40 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>} {
        }
        obelisk.sv.symbol.subroutine @s22.randomize attributes {hierarchical_name = "middle::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 41 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 42 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s23.pre_randomize attributes {hierarchical_name = "middle::pre_randomize", is_builtin, name = "pre_randomize", node_id = 43 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 44 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s24.post_randomize attributes {hierarchical_name = "middle::post_randomize", is_builtin, name = "post_randomize", node_id = 45 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 46 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s25.get_randstate attributes {hierarchical_name = "middle::get_randstate", is_builtin, name = "get_randstate", node_id = 47 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 48 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s26.set_randstate attributes {hierarchical_name = "middle::set_randstate", is_builtin, name = "set_randstate", node_id = 49 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 50 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s27.state attributes {direction = 0 : i32, hierarchical_name = "middle::set_randstate.state", name = "state", node_id = 51 : i64, semantic_type = !obelisk.string} {
          }
        }
        obelisk.sv.symbol.subroutine @s28.srandom attributes {hierarchical_name = "middle::srandom", is_builtin, name = "srandom", node_id = 52 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 53 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s29.seed attributes {direction = 0 : i32, hierarchical_name = "middle::srandom.seed", name = "seed", node_id = 54 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s30.rand_mode attributes {hierarchical_name = "middle::rand_mode", is_builtin, name = "rand_mode", node_id = 55 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 56 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s31.on_ff attributes {direction = 0 : i32, hierarchical_name = "middle::rand_mode.on_ff", name = "on_ff", node_id = 57 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.subroutine @s32.constraint_mode attributes {hierarchical_name = "middle::constraint_mode", is_builtin, name = "constraint_mode", node_id = 58 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 59 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s33.on_ff attributes {direction = 0 : i32, hierarchical_name = "middle::constraint_mode.on_ff", name = "on_ff", node_id = 60 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s53.this attributes {hierarchical_name = "middle::this", is_compiler_generated, is_const, name = "this", node_id = 91 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.middle>} {
        }
      }
      obelisk.sv.type.class_type @s34.root attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "root", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "root", node_id = 61 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s34.root>, this_variable_path = "root::this", this_variable_symbol = @s1.$root::@s2::@s34.root::@s52.this} {
        obelisk.sv.symbol.class_property @s35.branch attributes {hierarchical_name = "root::branch", name = "branch", node_id = 62 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.middle>} {
        }
        obelisk.sv.symbol.subroutine @s36.randomize attributes {hierarchical_name = "root::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 63 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 64 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s37.pre_randomize attributes {hierarchical_name = "root::pre_randomize", is_builtin, name = "pre_randomize", node_id = 65 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 66 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s38.post_randomize attributes {hierarchical_name = "root::post_randomize", is_builtin, name = "post_randomize", node_id = 67 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 68 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s39.get_randstate attributes {hierarchical_name = "root::get_randstate", is_builtin, name = "get_randstate", node_id = 69 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 70 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s40.set_randstate attributes {hierarchical_name = "root::set_randstate", is_builtin, name = "set_randstate", node_id = 71 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 72 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s41.state attributes {direction = 0 : i32, hierarchical_name = "root::set_randstate.state", name = "state", node_id = 73 : i64, semantic_type = !obelisk.string} {
          }
        }
        obelisk.sv.symbol.subroutine @s42.srandom attributes {hierarchical_name = "root::srandom", is_builtin, name = "srandom", node_id = 74 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 75 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s43.seed attributes {direction = 0 : i32, hierarchical_name = "root::srandom.seed", name = "seed", node_id = 76 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s44.rand_mode attributes {hierarchical_name = "root::rand_mode", is_builtin, name = "rand_mode", node_id = 77 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 78 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s45.on_ff attributes {direction = 0 : i32, hierarchical_name = "root::rand_mode.on_ff", name = "on_ff", node_id = 79 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.subroutine @s46.constraint_mode attributes {hierarchical_name = "root::constraint_mode", is_builtin, name = "constraint_mode", node_id = 80 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 81 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s47.on_ff attributes {direction = 0 : i32, hierarchical_name = "root::constraint_mode.on_ff", name = "on_ff", node_id = 82 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s52.this attributes {hierarchical_name = "root::this", is_compiler_generated, is_const, name = "this", node_id = 90 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s34.root>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s48.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 83 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s49.top attributes {hierarchical_name = "top", name = "top", node_id = 84 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s50.value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 85 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s34.root>} {
        }
        obelisk.sv.symbol.procedural_block @s51 attributes {hierarchical_name = "top", node_id = 86 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 87 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 88 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s48.top::@s49.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 89 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s48.top::@s49.top::@s50.value, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s34.root>} {
              }
            }
          }
        }
      }
    }
  }
}


// A recursively nested object's constraint participates in the same solve as
// its random leaf. Every rand handle edge and null ancestor gates both the
// leaf and the descendant constraint block. Non-rand members are captured
// from the descendant object through the same null-safe path.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: simulation.class.field_ref {{.*}}[@__obelisk_class_s34_root_field___obelisk_rand_mode]
// CHECK: simulation.managed.is_null
// CHECK: arith.cmpi ne
// CHECK: simulation.class.field_ref {{.*}}[@__obelisk_class_s20_middle_field___obelisk_rand_mode]
// CHECK: arith.cmpi ne
// CHECK: simulation.managed.is_null
// CHECK: simulation.class.field_ref {{.*}}[@__obelisk_class_s3_leaf_field___obelisk_constraint_mode]
// CHECK: simulation.managed.is_null
// CHECK: simulation.class.field_ref {{.*}}[@__obelisk_class_s3_leaf_field_1]
// CHECK: simulation.random.solve {{.*}} captures(%{{.*}})
// CHECK: arith.cmpi sge, {{.*}}, %{{.*}} : i32
// CHECK: arith.cmpi sle, {{.*}}, %{{.*}} : i32
// CHECK: arith.cmpi sge, {{.*}}, %{{.*}} : i32
// CHECK: simulation.managed.store
