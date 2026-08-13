// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 18.5.7.1: a foreach constraint applies its body to every
// element. The checker carries conjunction state through a runtime queue loop.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: obelisk_sim.container.size
// CHECK: cf.cond_br
// CHECK: obelisk_sim.container.read
// CHECK: arith.cmpi ne
// CHECK: arith.andi
// CHECK-NOT: obelisk_sim.random.solve

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s25.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::limits", name = "limits", node_id = 5 : i64, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, sym_name = "s5.limits"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::c", name = "c", node_id = 6 : i64, sym_name = "s6.c", this_variable_path = "C::c.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.c::@s8.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.foreach attributes {loop_dimensions = [{has_iterator = true, has_static_range = false, iterator_path = "C::c.i", iterator_symbol = @s1.$root::@s2::@s3.C::@s6.c::@s7.i, iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>}], node_id = 8 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "C::limits", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.limits, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>} {
              }
              obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 10 : i64} {
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 11 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.element_select attributes {is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "C::limits", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.limits, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "C::c.i", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.c::@s7.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::c.this", is_compiler_generated, is_const, name = "this", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s8.this"} {
          }
          obelisk.sv.symbol.iterator attributes {array_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, hierarchical_name = "C::c.i", index_method_name = "", is_const, name = "i", node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.i"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 19 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s9.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s12.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 27 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 28 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 29 : i64, semantic_type = !obelisk.string, sym_name = "s14.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 30 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 31 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s16.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 33 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 34 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 36 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s19.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 37 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 38 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s20.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 47 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s25.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 39 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s21.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 40 : i64, sym_name = "s22.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 41 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s23.c"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 42 : i64, procedure_kind = 0 : i32, sym_name = "s24", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 43 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s21.top::@s22.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s21.top::@s22.top::@s23.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}


