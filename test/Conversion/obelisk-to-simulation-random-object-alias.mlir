// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "leaf", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "leaf", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s3.leaf", this_variable_path = "leaf::this", this_variable_symbol = @s1.$root::@s2::@s3.leaf::@s57.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "leaf::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "leaf::data", name = "data", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>, sym_name = "s5.data"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "leaf::c", name = "c", node_id = 6 : i64, sym_name = "s6.c", this_variable_path = "leaf::c.this", this_variable_symbol = @s1.$root::@s2::@s3.leaf::@s6.c::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 8 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 9 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "leaf::x", referenced_symbol = @s1.$root::@s2::@s3.leaf::@s4.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.conversion attributes {folded_constant = "32'd8", is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "8", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "leaf::c.this", is_compiler_generated, is_const, name = "this", node_id = 14 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s7.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::pre_randomize", is_pre_post_randomize, name = "pre_randomize", node_id = 15 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", this_variable_path = "leaf::pre_randomize.this", this_variable_symbol = @s1.$root::@s2::@s3.leaf::@s8.pre_randomize::@s9.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "leaf::pre_randomize.this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s9.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::post_randomize", is_pre_post_randomize, name = "post_randomize", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.post_randomize", this_variable_path = "leaf::post_randomize.this", this_variable_symbol = @s1.$root::@s2::@s3.leaf::@s10.post_randomize::@s11.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "leaf::post_randomize.this", is_compiler_generated, is_const, name = "this", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s11.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s12.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::get_randstate", is_builtin, name = "get_randstate", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s13.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::set_randstate", is_builtin, name = "set_randstate", node_id = 25 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s14.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf::set_randstate.state", name = "state", node_id = 27 : i64, semantic_type = !obelisk.string, sym_name = "s15.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::srandom", is_builtin, name = "srandom", node_id = 28 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s16.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 29 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf::srandom.seed", name = "seed", node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s17.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::rand_mode", is_builtin, name = "rand_mode", node_id = 31 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s18.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 32 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf::rand_mode.on_ff", name = "on_ff", node_id = 33 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s19.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf::constraint_mode", is_builtin, name = "constraint_mode", node_id = 34 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s20.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 35 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf::constraint_mode.on_ff", name = "on_ff", node_id = 36 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s21.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "leaf::this", is_compiler_generated, is_const, name = "this", node_id = 91 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s57.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "pair", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "pair", node_id = 37 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.pair>, sym_name = "s22.pair", this_variable_path = "pair::this", this_variable_symbol = @s1.$root::@s2::@s22.pair::@s56.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "pair::left", name = "left", node_id = 38 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s23.left"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "pair::right", name = "right", node_id = 39 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf>, sym_name = "s24.right"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "pair::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 40 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s25.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 41 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "pair::pre_randomize", is_builtin, name = "pre_randomize", node_id = 42 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s26.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "pair::post_randomize", is_builtin, name = "post_randomize", node_id = 44 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s27.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 45 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "pair::get_randstate", is_builtin, name = "get_randstate", node_id = 46 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s28.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 47 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "pair::set_randstate", is_builtin, name = "set_randstate", node_id = 48 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s29.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 49 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "pair::set_randstate.state", name = "state", node_id = 50 : i64, semantic_type = !obelisk.string, sym_name = "s30.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "pair::srandom", is_builtin, name = "srandom", node_id = 51 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s31.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 52 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "pair::srandom.seed", name = "seed", node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s32.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "pair::rand_mode", is_builtin, name = "rand_mode", node_id = 54 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s33.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 55 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "pair::rand_mode.on_ff", name = "on_ff", node_id = 56 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s34.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "pair::constraint_mode", is_builtin, name = "constraint_mode", node_id = 57 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s35.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 58 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "pair::constraint_mode.on_ff", name = "on_ff", node_id = 59 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s36.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "pair::this", is_compiler_generated, is_const, name = "this", node_id = 90 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.pair>, sym_name = "s56.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "root", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "root", node_id = 60 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s37.root>, sym_name = "s37.root", this_variable_path = "root::this", this_variable_symbol = @s1.$root::@s2::@s37.root::@s55.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "root::p", name = "p", node_id = 61 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.pair>, sym_name = "s38.p"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 62 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s39.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 63 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::pre_randomize", is_builtin, name = "pre_randomize", node_id = 64 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s40.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 65 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::post_randomize", is_builtin, name = "post_randomize", node_id = 66 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s41.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 67 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::get_randstate", is_builtin, name = "get_randstate", node_id = 68 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s42.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 69 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::set_randstate", is_builtin, name = "set_randstate", node_id = 70 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s43.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 71 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::set_randstate.state", name = "state", node_id = 72 : i64, semantic_type = !obelisk.string, sym_name = "s44.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::srandom", is_builtin, name = "srandom", node_id = 73 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s45.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 74 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::srandom.seed", name = "seed", node_id = 75 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s46.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::rand_mode", is_builtin, name = "rand_mode", node_id = 76 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s47.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 77 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::rand_mode.on_ff", name = "on_ff", node_id = 78 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s48.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::constraint_mode", is_builtin, name = "constraint_mode", node_id = 79 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s49.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 80 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::constraint_mode.on_ff", name = "on_ff", node_id = 81 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s50.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "root::this", is_compiler_generated, is_const, name = "this", node_id = 89 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s37.root>, sym_name = "s55.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 82 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s51.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 83 : i64, sym_name = "s52.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 84 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s37.root>, sym_name = "s53.r"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 85 : i64, procedure_kind = 0 : i32, sym_name = "s54", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 86 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 87 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s51.top::@s52.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 88 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s51.top::@s52.top::@s53.r, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s37.root>} {
              }
            }
          }
        }
      }
    }
  }
}

// Distinct static paths may reach one active random object. The lowering uses
// runtime identity to call lifecycle hooks once, constrain corresponding
// packed leaves to one value, and randomize each dynamic container field once.
// Non-aliased objects remain independent.
// CHECK-LABEL: obelisk_sim.func private @{{unit_[0-9]+}}{{.*}}obelisk_sim.hierarchical_name = "top"
// CHECK: %[[LEFT_HOOK_ID:.*]] = obelisk_sim.class.id %[[LEFT_HOOK_OBJECT:[0-9A-Za-z_]+]]
// CHECK: obelisk_sim.class.direct_call {{.*}} %[[LEFT_HOOK_OBJECT]]()
// CHECK: %[[RIGHT_HOOK_ID:.*]] = obelisk_sim.class.id %[[RIGHT_HOOK_OBJECT:[0-9A-Za-z_]+]]
// CHECK: %[[HOOK_ALIAS:.*]] = arith.cmpi eq, %[[RIGHT_HOOK_ID]], %[[LEFT_HOOK_ID]] : i64
// CHECK: %[[SEEN_HOOK:.*]] = arith.andi {{.*}}, %[[HOOK_ALIAS]] : i1
// CHECK: %[[NEW_HOOK:.*]] = arith.xori %[[SEEN_HOOK]], {{.*}} : i1
// CHECK: %[[CALL_HOOK:.*]] = arith.andi {{.*}}, %[[NEW_HOOK]] : i1
// CHECK: cf.cond_br %[[CALL_HOOK]], ^[[RIGHT_PRE:bb[0-9]+]], ^{{bb[0-9]+}}
// CHECK: ^[[RIGHT_PRE]]:
// CHECK: obelisk_sim.class.direct_call {{.*}} %[[RIGHT_HOOK_OBJECT]]()
// CHECK: ^{{bb[0-9]+}}({{.*}}: !obelisk_sim.packed_array<3 : 0 x i1>, {{.*}}: i1, %[[LEFT_OWNER_ID:.*]]: i64):
// CHECK: %[[LEFT_VALUE_ID:.*]] = obelisk_sim.class.id
// CHECK: ^{{bb[0-9]+}}({{.*}}: !obelisk_sim.packed_array<3 : 0 x i1>, {{.*}}: i1, %[[RIGHT_OWNER_ID:.*]]: i64):
// CHECK: %[[VALUE_ALIAS:.*]] = arith.cmpi eq, %[[LEFT_OWNER_ID]], %[[RIGHT_OWNER_ID]] : i64
// CHECK: %[[ACTIVE_ALIAS:.*]] = arith.andi {{.*}}, %[[VALUE_ALIAS]] : i1
// CHECK: %[[ALIAS_CAPTURE:.*]] = arith.extui %[[ACTIVE_ALIAS]] : i1 to i64
// CHECK: %[[RIGHT_VALUE_ID:.*]] = obelisk_sim.class.id
// CHECK: obelisk_sim.random.solve {{.*}} captures(%[[ALIAS_CAPTURE]])
// CHECK: %[[LEFT_CONTAINER_ID:.*]] = obelisk_sim.class.id
// CHECK: ^{{bb[0-9]+}}(%{{.*}}: !obelisk_sim.dynamic_array<i8>, %[[LEFT_CONTAINER_OWNER:.*]]: i64, {{.*}}: i1):
// CHECK: %[[RIGHT_CONTAINER_ID:.*]] = obelisk_sim.class.id
// CHECK: ^{{bb[0-9]+}}(%{{.*}}: !obelisk_sim.dynamic_array<i8>, %[[RIGHT_CONTAINER_OWNER:.*]]: i64, {{.*}}: i1):
// CHECK: %[[CONTAINER_ALIAS:.*]] = arith.cmpi eq, %[[RIGHT_CONTAINER_OWNER]], %[[LEFT_CONTAINER_OWNER]] : i64
// CHECK: %[[SEEN_CONTAINER:.*]] = arith.andi {{.*}}, %[[CONTAINER_ALIAS]] : i1
// CHECK: %[[NEW_CONTAINER:.*]] = arith.xori %[[SEEN_CONTAINER]], {{.*}} : i1
// CHECK: %[[RANDOMIZE_CONTAINER:.*]] = arith.andi {{.*}}, %[[NEW_CONTAINER]] : i1
// CHECK: cf.cond_br %[[RANDOMIZE_CONTAINER]], ^{{bb[0-9]+}}, ^{{bb[0-9]+}}
