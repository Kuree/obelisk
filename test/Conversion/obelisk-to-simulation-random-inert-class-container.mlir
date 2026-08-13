// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "abstract_leaf", implemented_interfaces = [], is_abstract = true, is_final = false, is_interface = false, is_uninstantiated = false, name = "abstract_leaf", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.abstract_leaf>, sym_name = "s3.abstract_leaf", this_variable_path = "abstract_leaf::this", this_variable_symbol = @s1.$root::@s2::@s3.abstract_leaf::@s35.this} {
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "abstract_leaf::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 4 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s4.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 5 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "abstract_leaf::pre_randomize", is_builtin, name = "pre_randomize", node_id = 6 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s5.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 7 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "abstract_leaf::post_randomize", is_builtin, name = "post_randomize", node_id = 8 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s6.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 9 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "abstract_leaf::get_randstate", is_builtin, name = "get_randstate", node_id = 10 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s7.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 11 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "abstract_leaf::set_randstate", is_builtin, name = "set_randstate", node_id = 12 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "abstract_leaf::set_randstate.state", name = "state", node_id = 14 : i64, semantic_type = !obelisk.string, sym_name = "s9.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "abstract_leaf::srandom", is_builtin, name = "srandom", node_id = 15 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "abstract_leaf::srandom.seed", name = "seed", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s11.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "abstract_leaf::rand_mode", is_builtin, name = "rand_mode", node_id = 18 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s12.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "abstract_leaf::rand_mode.on_ff", name = "on_ff", node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s13.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "abstract_leaf::constraint_mode", is_builtin, name = "constraint_mode", node_id = 21 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s14.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "abstract_leaf::constraint_mode.on_ff", name = "on_ff", node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s15.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "abstract_leaf::this", is_compiler_generated, is_const, name = "this", node_id = 54 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.abstract_leaf>, sym_name = "s35.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "root", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "root", node_id = 24 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s16.root>, sym_name = "s16.root", this_variable_path = "root::this", this_variable_symbol = @s1.$root::@s2::@s16.root::@s34.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "root::items", name = "items", node_id = 25 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.dynarray<!obelisk.class_handle<@s1.$root::@s2::@s3.abstract_leaf>>, sym_name = "s17.items"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 26 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s18.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::pre_randomize", is_builtin, name = "pre_randomize", node_id = 28 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s19.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 29 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::post_randomize", is_builtin, name = "post_randomize", node_id = 30 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s20.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 31 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::get_randstate", is_builtin, name = "get_randstate", node_id = 32 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s21.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 33 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::set_randstate", is_builtin, name = "set_randstate", node_id = 34 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s22.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 35 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::set_randstate.state", name = "state", node_id = 36 : i64, semantic_type = !obelisk.string, sym_name = "s23.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::srandom", is_builtin, name = "srandom", node_id = 37 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s24.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 38 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::srandom.seed", name = "seed", node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s25.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::rand_mode", is_builtin, name = "rand_mode", node_id = 40 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s26.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 41 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::rand_mode.on_ff", name = "on_ff", node_id = 42 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s27.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "root::constraint_mode", is_builtin, name = "constraint_mode", node_id = 43 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s28.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 44 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "root::constraint_mode.on_ff", name = "on_ff", node_id = 45 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s29.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "root::this", is_compiler_generated, is_const, name = "this", node_id = 53 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s16.root>, sym_name = "s34.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 46 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s30.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 47 : i64, sym_name = "s31.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 48 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s16.root>, sym_name = "s32.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 49 : i64, procedure_kind = 0 : i32, sym_name = "s33", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s30.top::@s31.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 52 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s30.top::@s31.top::@s32.value, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s16.root>} {
              }
            }
          }
        }
      }
    }
  }
}


// With no compatible concrete element class in the closed world, every
// element of the abstract-class array is necessarily null. IEEE 1800-2023
// 18.4 requires no allocation or randomization and retains its unconstrained
// size, so the call neither reads/writes the container nor advances RNG state.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-NOT: @__obelisk_class_s16_root_field_0
// CHECK-NOT: obelisk_sim.random
// CHECK-NOT: obelisk_sim.container
// CHECK-NOT: obelisk_sim.managed.store
// CHECK: obelisk_sim.return
