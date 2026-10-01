// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 8.11: a method of the owning class may name a class property
// without `this`, which the frontend spells as a plain reference to the
// property rather than as a member access.  Section 8.3 makes that property a
// variable of the object and 7.4.6 makes an element of it assignable, so
// `q[1] = 7` rebuilds the whole property through its managed reference the way
// the qualified `obj.q[1] = 7` does -- the object's storage exposes no
// interior reference to write through.

module {
  obelisk.sv.symbol.definition @s0.t attributes {
    definition_kind = 0 : i32, hierarchical_name = "t", name = "t",
    node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {
      obelisk.sv.type.class_type @s3.C attributes {
        bitstream_width = 128 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "C", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "C", node_id = 3 : i64,
        semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>,
        this_variable_path = "C::this",
        this_variable_symbol = @s1.$root::@s2::@s3.C::@s23.this
      } {
        obelisk.sv.symbol.class_property @s4.q attributes {
          hierarchical_name = "C::q", name = "q", node_id = 4 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.integral<32, true, false, 31 : 0, int>>
        } {
        }
        obelisk.sv.symbol.subroutine @s5.setit attributes {
          hierarchical_name = "C::setit", name = "setit", node_id = 5 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
          subroutine_kind = 0 : i32,
          this_variable_path = "C::setit.this",
          this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.setit::@s6.this,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 6 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = true, node_id = 7 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
              obelisk.sv.expression.element_select attributes {
                is_signed = true, node_id = 8 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 9 : i64,
                  referenced_path = "C::q",
                  referenced_symbol = @s1.$root::@s2::@s3.C::@s4.q,
                  semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.integral<32, true, false, 31 : 0, int>>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "1", is_declared_unsized = true,
                  is_signed = true, node_id = 10 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
              }
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "7", is_declared_unsized = true,
                is_signed = true, node_id = 11 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
            }
          }
          obelisk.sv.symbol.variable @s6.this attributes {
            hierarchical_name = "C::setit.this", is_compiler_generated,
            is_const, name = "this", node_id = 12 : i64,
            semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
          } {
          }
        }
        obelisk.sv.symbol.subroutine @s7.randomize attributes {
          hierarchical_name = "C::randomize", is_builtin, is_declared_virtual,
          is_randomize, is_virtual, name = "randomize", node_id = 13 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
          subroutine_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s8.pre_randomize attributes {
          hierarchical_name = "C::pre_randomize", is_builtin,
          name = "pre_randomize", node_id = 15 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
          subroutine_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s9.post_randomize attributes {
          hierarchical_name = "C::post_randomize", is_builtin,
          name = "post_randomize", node_id = 17 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
          subroutine_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 18 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s10.get_randstate attributes {
          hierarchical_name = "C::get_randstate", is_builtin,
          name = "get_randstate", node_id = 19 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>,
          subroutine_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s11.set_randstate attributes {
          hierarchical_name = "C::set_randstate", is_builtin,
          name = "set_randstate", node_id = 21 : i64,
          semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>,
          subroutine_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s12.state attributes {
            direction = 0 : i32, hierarchical_name = "C::set_randstate.state",
            name = "state", node_id = 23 : i64,
            semantic_type = !obelisk.string
          } {
          }
        }
        obelisk.sv.symbol.subroutine @s13.srandom attributes {
          hierarchical_name = "C::srandom", is_builtin, name = "srandom",
          node_id = 24 : i64,
          semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>,
          subroutine_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 25 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s14.seed attributes {
            direction = 0 : i32, hierarchical_name = "C::srandom.seed",
            name = "seed", node_id = 26 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
          } {
          }
        }
        obelisk.sv.symbol.subroutine @s15.rand_mode attributes {
          hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode",
          node_id = 27 : i64,
          semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>,
          subroutine_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 28 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s16.on_ff attributes {
            direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff",
            name = "on_ff", node_id = 29 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
          } {
          }
        }
        obelisk.sv.symbol.subroutine @s17.constraint_mode attributes {
          hierarchical_name = "C::constraint_mode", is_builtin,
          name = "constraint_mode", node_id = 30 : i64,
          semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>,
          subroutine_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 31 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s18.on_ff attributes {
            direction = 0 : i32,
            hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff",
            node_id = 32 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
          } {
          }
        }
        obelisk.sv.symbol.variable @s23.this attributes {
          hierarchical_name = "C::this", is_compiler_generated, is_const,
          name = "this", node_id = 40 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
        } {
        }
      }
    }
    obelisk.sv.symbol.instance @s19.t attributes {
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 33 : i64, referenced_path = "t", referenced_symbol = @s0.t
    } {
      obelisk.sv.symbol.instance_body @s20.t attributes {
        hierarchical_name = "t", name = "t", node_id = 34 : i64,
        time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable @s21.c attributes {
          hierarchical_name = "t.c", lifetime = 1 : i32, name = "c",
          node_id = 35 : i64,
          semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
        } {
        }
        obelisk.sv.symbol.procedural_block @s22 attributes {
          hierarchical_name = "t", node_id = 36 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 37 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 0 : i64, callee_name = "setit",
              constraint_restrictions = [], defaulted_arguments = array<i64>,
              has_inline_constraints = false, has_iterator_expression = false,
              has_output_arguments = false, has_this_class = true,
              is_signed = false, is_super_class = false,
              is_system_call = false, node_id = 38 : i64,
              referenced_path = "C::setit",
              referenced_symbol = @s1.$root::@s2::@s3.C::@s5.setit,
              semantic_type = !obelisk.void, subroutine_kind = 0 : i32
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 39 : i64, referenced_path = "t.c",
                referenced_symbol = @s1.$root::@s19.t::@s20.t::@s21.c,
                semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>
              } {
              }
            }
          }
        }
      }
    }
  }
}



// CHECK-LABEL: simulation.func private @unit_0
// CHECK-SAME: %[[THIS:[a-z0-9]+]]: !simulation.class_handle<@[[CLASS:[A-Za-z0-9_.$]+]]>
// CHECK-SAME: simulation.hierarchical_name = "C::setit"
// CHECK: %[[SEVEN:.*]] = arith.constant 7 : i32
// CHECK: %[[FIELD:.*]] = simulation.class.field_ref %[[THIS]][@[[PROP:[A-Za-z0-9_.$]+]]]
// CHECK: %[[OLD:.*]] = simulation.managed.load %[[FIELD]]
// CHECK: %[[NEW:.*]] = simulation.aggregate.insert %[[SEVEN]] into %[[OLD]][1]
// CHECK: simulation.managed.store %[[NEW]] to %[[FIELD]]
