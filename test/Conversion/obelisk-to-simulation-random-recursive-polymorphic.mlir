// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "leaf_base", implemented_interfaces = [], is_abstract = true, is_final = false, is_interface = false, is_uninstantiated = false, name = "leaf_base", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf_base>, sym_name = "s3.leaf_base", this_variable_path = "leaf_base::this", this_variable_symbol = @s1.$root::@s2::@s3.leaf_base::@s83.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "leaf_base::common", name = "common", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.common"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_base::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 5 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s5.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_base::pre_randomize", is_builtin, name = "pre_randomize", node_id = 7 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s6.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 8 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_base::post_randomize", is_builtin, name = "post_randomize", node_id = 9 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s7.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 10 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_base::get_randstate", is_builtin, name = "get_randstate", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s8.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_base::set_randstate", is_builtin, name = "set_randstate", node_id = 13 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_base::set_randstate.state", name = "state", node_id = 15 : i64, semantic_type = !obelisk.string, sym_name = "s10.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_base::srandom", is_builtin, name = "srandom", node_id = 16 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_base::srandom.seed", name = "seed", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_base::rand_mode", is_builtin, name = "rand_mode", node_id = 19 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_base::rand_mode.on_ff", name = "on_ff", node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s14.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_base::constraint_mode", is_builtin, name = "constraint_mode", node_id = 22 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 23 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_base::constraint_mode.on_ff", name = "on_ff", node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "leaf_base::this", is_compiler_generated, is_const, name = "this", node_id = 126 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf_base>, sym_name = "s83.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf_base>, bitstream_width = 40 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "leaf_a", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "leaf_a", node_id = 25 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.leaf_a>, sym_name = "s17.leaf_a", this_variable_path = "leaf_a::this", this_variable_symbol = @s1.$root::@s2::@s17.leaf_a::@s82.this} {
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "leaf_a::common", name = "common", node_id = 26 : i64, sym_name = "s18.common"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "leaf_a::a", name = "a", node_id = 27 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>, sym_name = "s19.a"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_a::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 28 : i64, override_path = "leaf_base::randomize", override_symbol = @s1.$root::@s2::@s3.leaf_base::@s5.randomize, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s20.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 29 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_a::pre_randomize", is_builtin, name = "pre_randomize", node_id = 30 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s21.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 31 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_a::post_randomize", is_builtin, name = "post_randomize", node_id = 32 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s22.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 33 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_a::get_randstate", is_builtin, name = "get_randstate", node_id = 34 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s23.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 35 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_a::set_randstate", is_builtin, name = "set_randstate", node_id = 36 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s24.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 37 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_a::set_randstate.state", name = "state", node_id = 38 : i64, semantic_type = !obelisk.string, sym_name = "s25.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_a::srandom", is_builtin, name = "srandom", node_id = 39 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s26.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 40 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_a::srandom.seed", name = "seed", node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s27.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_a::rand_mode", is_builtin, name = "rand_mode", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s28.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_a::rand_mode.on_ff", name = "on_ff", node_id = 44 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s29.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_a::constraint_mode", is_builtin, name = "constraint_mode", node_id = 45 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s30.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 46 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_a::constraint_mode.on_ff", name = "on_ff", node_id = 47 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s31.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "leaf_a::this", is_compiler_generated, is_const, name = "this", node_id = 125 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.leaf_a>, sym_name = "s82.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf_base>, bitstream_width = 48 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "leaf_b", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "leaf_b", node_id = 48 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s32.leaf_b>, sym_name = "s32.leaf_b", this_variable_path = "leaf_b::this", this_variable_symbol = @s1.$root::@s2::@s32.leaf_b::@s81.this} {
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "leaf_b::common", name = "common", node_id = 49 : i64, sym_name = "s33.common"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "leaf_b::b", name = "b", node_id = 50 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<16, true, false, 15 : 0, shortint>, sym_name = "s34.b"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_b::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 51 : i64, override_path = "leaf_base::randomize", override_symbol = @s1.$root::@s2::@s3.leaf_base::@s5.randomize, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s35.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 52 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_b::pre_randomize", is_builtin, name = "pre_randomize", node_id = 53 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s36.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 54 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_b::post_randomize", is_builtin, name = "post_randomize", node_id = 55 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s37.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 56 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_b::get_randstate", is_builtin, name = "get_randstate", node_id = 57 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s38.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 58 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_b::set_randstate", is_builtin, name = "set_randstate", node_id = 59 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s39.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 60 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_b::set_randstate.state", name = "state", node_id = 61 : i64, semantic_type = !obelisk.string, sym_name = "s40.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_b::srandom", is_builtin, name = "srandom", node_id = 62 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s41.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 63 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_b::srandom.seed", name = "seed", node_id = 64 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s42.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_b::rand_mode", is_builtin, name = "rand_mode", node_id = 65 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s43.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 66 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_b::rand_mode.on_ff", name = "on_ff", node_id = 67 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s44.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "leaf_b::constraint_mode", is_builtin, name = "constraint_mode", node_id = 68 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s45.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 69 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "leaf_b::constraint_mode.on_ff", name = "on_ff", node_id = 70 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s46.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "leaf_b::this", is_compiler_generated, is_const, name = "this", node_id = 124 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s32.leaf_b>, sym_name = "s81.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "middle", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "middle", node_id = 71 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s47.middle>, sym_name = "s47.middle", this_variable_path = "middle::this", this_variable_symbol = @s1.$root::@s2::@s47.middle::@s80.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "middle::l", name = "l", node_id = 72 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.leaf_base>, sym_name = "s48.l"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 73 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s49.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 74 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::pre_randomize", is_builtin, name = "pre_randomize", node_id = 75 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s50.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 76 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::post_randomize", is_builtin, name = "post_randomize", node_id = 77 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s51.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 78 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::get_randstate", is_builtin, name = "get_randstate", node_id = 79 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s52.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 80 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::set_randstate", is_builtin, name = "set_randstate", node_id = 81 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s53.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 82 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "middle::set_randstate.state", name = "state", node_id = 83 : i64, semantic_type = !obelisk.string, sym_name = "s54.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::srandom", is_builtin, name = "srandom", node_id = 84 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s55.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 85 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "middle::srandom.seed", name = "seed", node_id = 86 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s56.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::rand_mode", is_builtin, name = "rand_mode", node_id = 87 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s57.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 88 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "middle::rand_mode.on_ff", name = "on_ff", node_id = 89 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s58.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "middle::constraint_mode", is_builtin, name = "constraint_mode", node_id = 90 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s59.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 91 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "middle::constraint_mode.on_ff", name = "on_ff", node_id = 92 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s60.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "middle::this", is_compiler_generated, is_const, name = "this", node_id = 123 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s47.middle>, sym_name = "s80.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "root", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "root", node_id = 93 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s61.root>, sym_name = "s61.root", this_variable_path = "root::this", this_variable_symbol = @s1.$root::@s2::@s61.root::@s79.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "root::m", name = "m", node_id = 94 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s47.middle>, sym_name = "s62.m"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 95 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s63.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 96 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::pre_randomize", is_builtin, name = "pre_randomize", node_id = 97 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s64.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 98 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::post_randomize", is_builtin, name = "post_randomize", node_id = 99 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s65.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 100 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::get_randstate", is_builtin, name = "get_randstate", node_id = 101 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s66.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 102 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::set_randstate", is_builtin, name = "set_randstate", node_id = 103 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s67.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 104 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::set_randstate.state", name = "state", node_id = 105 : i64, semantic_type = !obelisk.string, sym_name = "s68.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::srandom", is_builtin, name = "srandom", node_id = 106 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s69.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 107 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::srandom.seed", name = "seed", node_id = 108 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s70.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::rand_mode", is_builtin, name = "rand_mode", node_id = 109 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s71.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 110 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::rand_mode.on_ff", name = "on_ff", node_id = 111 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s72.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::constraint_mode", is_builtin, name = "constraint_mode", node_id = 112 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s73.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 113 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::constraint_mode.on_ff", name = "on_ff", node_id = 114 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s74.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "root::this", is_compiler_generated, is_const, name = "this", node_id = 122 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s61.root>, sym_name = "s79.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 115 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s75.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 116 : i64, sym_name = "s76.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 117 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s61.root>, sym_name = "s77.r"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 118 : i64, procedure_kind = 0 : i32, sym_name = "s78", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 119 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 120 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s75.top::@s76.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 121 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s75.top::@s76.top::@s77.r, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s61.root>} {
              }
            }
          }
        }
      }
    }
  }
}


// A polymorphic descendant is inspected only after all ancestor handles are
// proven non-null. Its runtime class selects a frozen whole-graph solver plan;
// each plan commits the common base leaf and only its concrete derived leaf.
// The null descendant selects the null plan without replacing any handle.
// CHECK-LABEL: obelisk_sim.func private @{{unit_[0-9]+}}{{.*}}obelisk_sim.hierarchical_name = "top"
// CHECK-DAG: %[[MIDDLE_REF:.*]] = obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s61_root_field_0]
// CHECK-DAG: %[[DISPATCH_MIDDLE:.*]] = obelisk_sim.managed.load %[[MIDDLE_REF]]
// CHECK-DAG: obelisk_sim.managed.is_null %[[DISPATCH_MIDDLE]]
// CHECK-DAG: %[[LEAF_REF:.*]] = obelisk_sim.class.field_ref %[[DISPATCH_MIDDLE]][@__obelisk_class_s47_middle_field_0]
// CHECK-DAG: %[[DISPATCH_LEAF:.*]] = obelisk_sim.managed.load %[[LEAF_REF]]
// CHECK-DAG: obelisk_sim.managed.is_null %[[DISPATCH_LEAF]]
// CHECK-DAG: obelisk_sim.class.is_instance %[[DISPATCH_LEAF]] is @__obelisk_class_s17_leaf_a
// CHECK-DAG: obelisk_sim.class.is_instance %[[DISPATCH_LEAF]] is @__obelisk_class_s32_leaf_b
// CHECK-DAG: obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s3_leaf_base_field_0] {{.*}}class_handle<@__obelisk_class_s17_leaf_a>
// CHECK-DAG: obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s17_leaf_a_field_0]
// CHECK-DAG: obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s3_leaf_base_field_0] {{.*}}class_handle<@__obelisk_class_s32_leaf_b>
// CHECK-DAG: obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s32_leaf_b_field_0]
// CHECK-NOT: obelisk_sim.managed.store {{.*}} to %[[MIDDLE_REF]] :
// CHECK-NOT: obelisk_sim.managed.store {{.*}} to %[[LEAF_REF]] :
