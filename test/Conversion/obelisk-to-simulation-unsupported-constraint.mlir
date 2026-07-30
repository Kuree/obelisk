// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_constraint", name = "unsupported_constraint", node_id = 0 : i64, sym_name = "s0.unsupported_constraint"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "constrained", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "constrained", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s3.constrained", this_variable_path = "constrained::this", this_variable_symbol = @s1.$root::@s2::@s3.constrained::@s21.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "constrained::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "constrained::bounds", name = "bounds", node_id = 5 : i64, sym_name = "s5.bounds", this_variable_path = "constrained::bounds.this", this_variable_symbol = @s1.$root::@s2::@s3.constrained::@s5.bounds::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "constrained::value", referenced_symbol = @s1.$root::@s2::@s3.constrained::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "constrained::bounds.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "constrained::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "constrained::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "constrained::this", is_compiler_generated, is_const, name = "this", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.constrained>, sym_name = "s21.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_constraint", is_uninstantiated = false, name = "unsupported_constraint", node_id = 32 : i64, referenced_path = "unsupported_constraint", referenced_symbol = @s0.unsupported_constraint, sym_name = "s19.unsupported_constraint"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_constraint", name = "unsupported_constraint", node_id = 33 : i64, sym_name = "s20.unsupported_constraint"} {
      }
    }
  }
}

// CHECK: unsupported semantic construct in the first simulation slice
