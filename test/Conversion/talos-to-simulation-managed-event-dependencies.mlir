// RUN: talos-opt %s --verify-diagnostics '--lower-talos-to-sim=opt-level=0' \
// RUN:   | FileCheck %s

// IEEE 1800-2023 9.4.2: an event expression reevaluates when an object
// member, aggregate element, or dynamic array size that it reads changes,
// not only when the declaration's handle is replaced. A method call watches
// the properties of its receiver.
//
// class C;
//   int v;
//   function int get(); return v; endfunction
// endclass
// module top;
//   int data[];
//   C a;
//   initial @(data.size());
//   initial @(a.get());
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      talos.sv.type.class_type @s3.C attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s26.this} {
        talos.sv.symbol.class_property @s4.v attributes {hierarchical_name = "C::v", name = "v", node_id = 4 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.subroutine @s5.get attributes {hierarchical_name = "C::get", name = "get", node_id = 5 : i64, return_variable_path = "C::get.get", return_variable_symbol = @s1.$root::@s2::@s3.C::@s5.get::@s6.get, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, this_variable_path = "C::get.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.get::@s7.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.return attributes {node_id = 6 : i64} {
            talos.sv.expression.named_value attributes {is_signed = true, node_id = 7 : i64, referenced_path = "C::v", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.v, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            }
          }
          talos.sv.symbol.variable @s6.get attributes {hierarchical_name = "C::get.get", is_compiler_generated, name = "get", node_id = 8 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
          }
          talos.sv.symbol.variable @s7.this attributes {hierarchical_name = "C::get.this", is_compiler_generated, is_const, name = "this", node_id = 9 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        talos.sv.symbol.subroutine @s8.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 10 : i64, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 11 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s9.pre_randomize attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 12 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s10.post_randomize attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 14 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s11.get_randstate attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 16 : i64, semantic_type = !talos.subroutine<() -> !talos.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        talos.sv.symbol.subroutine @s12.set_randstate attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 18 : i64, semantic_type = !talos.subroutine<(!talos.string) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 19 : i64} {
          }
          talos.sv.symbol.formal_argument @s13.state attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 20 : i64, semantic_type = !talos.string} {
          }
        }
        talos.sv.symbol.subroutine @s14.srandom attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 21 : i64, semantic_type = !talos.subroutine<(!talos.integral<32, true, false, 31 : 0, int>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 22 : i64} {
          }
          talos.sv.symbol.formal_argument @s15.seed attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 23 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
          }
        }
        talos.sv.symbol.subroutine @s16.rand_mode attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 24 : i64, semantic_type = !talos.subroutine<(!talos.integral<1, false, false, 0 : 0, bit>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 25 : i64} {
          }
          talos.sv.symbol.formal_argument @s17.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 26 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        talos.sv.symbol.subroutine @s18.constraint_mode attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 27 : i64, semantic_type = !talos.subroutine<(!talos.integral<1, false, false, 0 : 0, bit>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 28 : i64} {
          }
          talos.sv.symbol.formal_argument @s19.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 29 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        talos.sv.symbol.variable @s26.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 46 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    talos.sv.symbol.instance @s20.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 30 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s21.top attributes {hierarchical_name = "top", name = "top", node_id = 31 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s22.data attributes {hierarchical_name = "top.data", lifetime = 1 : i32, name = "data", node_id = 32 : i64, semantic_type = !talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>} {
        }
        talos.sv.symbol.variable @s23.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 33 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
        }
        talos.sv.symbol.procedural_block @s24 attributes {hierarchical_name = "top", node_id = 34 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.timed attributes {node_id = 35 : i64} {
            talos.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 36 : i64} {
              talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 37 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "top.data", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22.data, semantic_type = !talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>} {
                }
              }
            }
            talos.sv.statement.empty attributes {node_id = 39 : i64} {
            }
          }
        }
        talos.sv.symbol.procedural_block @s25 attributes {hierarchical_name = "top", node_id = 40 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.timed attributes {node_id = 41 : i64} {
            talos.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 42 : i64} {
              talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "get", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = true, is_super_class = false, is_system_call = false, node_id = 43 : i64, referenced_path = "C::get", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.get, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s23.a, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
            talos.sv.statement.empty attributes {node_id = 45 : i64} {
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: %[[ARRAY:.*]] = simulation.ref.load %arg1 : !simulation.ref<!simulation.dynamic_array<i32>>
// CHECK: %[[SIZE:.*]] = simulation.managed.watch container_size %[[ARRAY]]
// CHECK: simulation.observer.bind {{.*}}values(%arg1, %arg1, %[[SIZE]] :

// CHECK-LABEL: simulation.func private @unit_2(
// CHECK: simulation.class.direct_call @unit_0
// CHECK: %[[OBJECT:.*]] = simulation.ref.load %arg1
// CHECK: %[[FIELD:.*]] = simulation.class.field_ref %[[OBJECT]][@__talos_class_s3_C_field_0]
// CHECK: %[[WATCH:.*]] = simulation.managed.watch field %[[FIELD]]
// CHECK: simulation.observer.bind {{.*}}values(%arg1, %arg1, %[[WATCH]] :
