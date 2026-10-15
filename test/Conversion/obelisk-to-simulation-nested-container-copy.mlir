// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Source:
//   typedef int r_t[];
//   class c_t; r_t p[2]; endclass
//   r_t a[2], b[2], r;
//   c_t c;
//   initial begin b = a; c.p[1] = r; end
//
// IEEE 1800-2023 7.6: assigning a fixed unpacked array copies its elements, so
// containers nested in the source are cloned instead of shared with `b`.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[A0:.+]] = simulation.ref.load
// CHECK: %[[A1:.+]] = simulation.ref.load
// CHECK: %[[C0:.+]] = simulation.container.clone %[[A0]]
// CHECK: %[[C1:.+]] = simulation.container.clone %[[A1]]
// CHECK: %[[B:.+]] = simulation.aggregate.construct %[[C0]], %[[C1]]
// CHECK: simulation.ref.store %[[B]] to
//
// Writing one element of a class property rebuilds the property value. Only
// the assigned row is copied; the sibling keeps its container identity, so the
// rebuilt value is stored without cloning again.
// CHECK: %[[R:.+]] = simulation.ref.load
// CHECK: %[[P:.+]] = simulation.managed.load
// CHECK-NEXT: %[[RC:.+]] = simulation.container.clone %[[R]]
// CHECK-NEXT: %[[U:.+]] = simulation.aggregate.insert %[[RC]] into %[[P]][1]
// CHECK-NEXT: simulation.managed.store %[[U]] to

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.m attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @s0.m} {
      obelisk.sv.symbol.instance_body @s4.m attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "m", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.type.type_alias @s5.r_t attributes {hierarchical_name = "m.r_t", name = "r_t", node_id = 5 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64>}]} {
        }
        obelisk.sv.type.class_type @s6.c_t attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "m.c_t", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "c_t", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.m::@s6.c_t>, this_variable_path = "m.c_t::this", this_variable_symbol = @s1.$root::@s3.m::@s4.m::@s6.c_t::@s25.this} {
          obelisk.sv.symbol.class_property @s7.p attributes {hierarchical_name = "m.c_t::p", name = "p", node_id = 7 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64: 0>}]} {
          }
          obelisk.sv.symbol.subroutine @s8.randomize attributes {hierarchical_name = "m.c_t::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 8 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine @s9.pre_randomize attributes {hierarchical_name = "m.c_t::pre_randomize", is_builtin, name = "pre_randomize", node_id = 10 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine @s10.post_randomize attributes {hierarchical_name = "m.c_t::post_randomize", is_builtin, name = "post_randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 13 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine @s11.get_randstate attributes {hierarchical_name = "m.c_t::get_randstate", is_builtin, name = "get_randstate", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 15 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine @s12.set_randstate attributes {hierarchical_name = "m.c_t::set_randstate", is_builtin, name = "set_randstate", node_id = 16 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 17 : i64} {
            }
            obelisk.sv.symbol.formal_argument @s13.state attributes {direction = 0 : i32, hierarchical_name = "m.c_t::set_randstate.state", name = "state", node_id = 18 : i64, semantic_type = !obelisk.string} {
            }
          }
          obelisk.sv.symbol.subroutine @s14.srandom attributes {hierarchical_name = "m.c_t::srandom", is_builtin, name = "srandom", node_id = 19 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 20 : i64} {
            }
            obelisk.sv.symbol.formal_argument @s15.seed attributes {direction = 0 : i32, hierarchical_name = "m.c_t::srandom.seed", name = "seed", node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.subroutine @s16.rand_mode attributes {hierarchical_name = "m.c_t::rand_mode", is_builtin, name = "rand_mode", node_id = 22 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 23 : i64} {
            }
            obelisk.sv.symbol.formal_argument @s17.on_ff attributes {direction = 0 : i32, hierarchical_name = "m.c_t::rand_mode.on_ff", name = "on_ff", node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
          obelisk.sv.symbol.subroutine @s18.constraint_mode attributes {hierarchical_name = "m.c_t::constraint_mode", is_builtin, name = "constraint_mode", node_id = 25 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 26 : i64} {
            }
            obelisk.sv.symbol.formal_argument @s19.on_ff attributes {direction = 0 : i32, hierarchical_name = "m.c_t::constraint_mode.on_ff", name = "on_ff", node_id = 27 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
          obelisk.sv.symbol.variable @s25.this attributes {hierarchical_name = "m.c_t::this", is_compiler_generated, is_const, name = "this", node_id = 46 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.m::@s6.c_t>} {
          }
        }
        obelisk.sv.symbol.variable @s20.a attributes {hierarchical_name = "m.a", lifetime = 1 : i32, name = "a", node_id = 28 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64: 0>}]} {
        }
        obelisk.sv.symbol.variable @s21.b attributes {hierarchical_name = "m.b", lifetime = 1 : i32, name = "b", node_id = 29 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64: 0>}]} {
        }
        obelisk.sv.symbol.variable @s22.r attributes {hierarchical_name = "m.r", lifetime = 1 : i32, name = "r", node_id = 30 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64>}]} {
        }
        obelisk.sv.symbol.variable @s23.c attributes {hierarchical_name = "m.c", lifetime = 1 : i32, name = "c", node_id = 31 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.m::@s6.c_t>} {
        }
        obelisk.sv.symbol.procedural_block @s24 attributes {hierarchical_name = "m", node_id = 32 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 33 : i64} {
            obelisk.sv.statement.list attributes {node_id = 34 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 36 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64: 0>}]} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "m.b", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s21.b, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64: 0>}]} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "m.a", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s20.a, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64: 0>}]} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 39 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 40 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64>}]} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 41 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64>}]} {
                    obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "p", node_id = 42 : i64, referenced_path = "m.c_t::p", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s6.c_t::@s7.p, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64: 0>}]} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, referenced_path = "m.c", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s23.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.m::@s6.c_t>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "m.r", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s22.r, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.m::@s5.r_t], path = array<i64>}]} {
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

