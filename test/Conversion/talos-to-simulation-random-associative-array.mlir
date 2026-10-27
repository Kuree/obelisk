// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 18.4: every element of a rand associative array is
// randomized; its keys are not randomizable. randomize() walks the existing
// keys in order and writes a random value at each.
//
// class C; rand bit [3:0] a[int]; endclass
// module top; C c; initial begin c = new; void'(c.randomize()); end endmodule

// CHECK: %[[ARRAY:.+]] = simulation.managed.load %{{.+}} : !simulation.managed_ref<!simulation.assoc_array<i32, !simulation.packed_array<3 : 0 x i1>, true, false>
// CHECK: ^[[HEADER:bb[0-9]+]](%[[KEY:.+]]: i32, %[[PRESENT:.+]]: i1, %{{.+}}: i64):
// CHECK-NEXT: cf.cond_br %[[PRESENT]], ^[[BODY:bb[0-9]+]], ^{{bb[0-9]+}}
// CHECK: ^[[BODY]]:
// CHECK: %[[VALUE:.+]] = simulation.packed.unflatten %{{.+}} : (i4) -> !simulation.packed_array<3 : 0 x i1>
// CHECK-NEXT: simulation.assoc.write %[[ARRAY]], %[[KEY]], %[[VALUE]]
// CHECK: simulation.assoc.traverse %[[ARRAY]], %{{.+}} {direction = #simulation.assoc_traversal_direction<forward>, endpoint = true}
// CHECK: simulation.assoc.traverse %[[ARRAY]], %[[KEY]] {direction = #simulation.assoc_traversal_direction<forward>, endpoint = false}

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      talos.sv.type.class_type @s3.C attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s21.this} {
        talos.sv.symbol.class_property @s4.a attributes {hierarchical_name = "C::a", name = "a", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !talos.assoc<!talos.integral<32, true, false, 31 : 0, int>, !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, false>} {
        }
        talos.sv.symbol.subroutine @s5.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 5 : i64, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 6 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s6.pre_randomize attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 7 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 8 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s7.post_randomize attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 9 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 10 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s8.get_randstate attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 11 : i64, semantic_type = !talos.subroutine<() -> !talos.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s9.set_randstate attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 13 : i64, semantic_type = !talos.subroutine<(!talos.string) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 14 : i64} {
          }
          talos.sv.symbol.formal_argument @s10.state attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 15 : i64, semantic_type = !talos.string} {
          }
        }
        talos.sv.symbol.subroutine @s11.srandom attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 16 : i64, semantic_type = !talos.subroutine<(!talos.integral<32, true, false, 31 : 0, int>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 17 : i64} {
          }
          talos.sv.symbol.formal_argument @s12.seed attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 18 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
          }
        }
        talos.sv.symbol.subroutine @s13.rand_mode attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 19 : i64, semantic_type = !talos.subroutine<(!talos.integral<1, false, false, 0 : 0, bit>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 20 : i64} {
          }
          talos.sv.symbol.formal_argument @s14.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 21 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        talos.sv.symbol.subroutine @s15.constraint_mode attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 22 : i64, semantic_type = !talos.subroutine<(!talos.integral<1, false, false, 0 : 0, bit>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 23 : i64} {
          }
          talos.sv.symbol.formal_argument @s16.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 24 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        talos.sv.symbol.variable @s21.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 38 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    talos.sv.symbol.instance @s17.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 25 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s18.top attributes {hierarchical_name = "top", name = "top", node_id = 26 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s19.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 27 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
        }
        talos.sv.symbol.procedural_block @s20 attributes {hierarchical_name = "top", node_id = 28 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 29 : i64} {
            talos.sv.statement.list attributes {node_id = 30 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 31 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 32 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s17.top::@s18.top::@s19.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                  }
                  talos.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 34 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 35 : i64} {
                talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 36 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s17.top::@s18.top} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s17.top::@s18.top::@s19.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
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

