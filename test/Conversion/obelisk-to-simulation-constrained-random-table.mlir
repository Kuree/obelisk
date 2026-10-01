// RUN: %if z3 %{ obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=TABLE %}
// RUN: %if !z3 %{ obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=TABLE-FALLBACK %}

// The even-value constraint has eight correlated solutions. Z3 enumerates
// the complete sorted table, and generated MLIR indexes it with three random
// bits, producing every legal assignment uniformly on the all-enabled path.
// Partial modes retain masked checking and runtime solving.
// TABLE-LABEL: simulation.func private @unit_1
// TABLE: %[[RAW:.*]] = arith.andi {{.*}}, {{.*}} : i64
// TABLE: simulation.managed.store
// TABLE: %[[INDEX:.*]] = arith.andi %[[COUNTER:.*]], {{.*}} : i64
// TABLE: %[[IS_ONE:.*]] = arith.cmpi eq, %[[INDEX]], {{.*}} : i64
// TABLE: %[[SELECT_ONE:.*]] = arith.select %[[IS_ONE]], {{.*}}, {{.*}} : i64
// TABLE-COUNT-5: arith.select
// TABLE: %[[ASSIGNMENT:.*]] = arith.select
// TABLE: simulation.random.solve {{.*}} mutable
// TABLE: arith.trunci {{.*}} : i64 to i4
// TABLE: simulation.managed.store

// TABLE-FALLBACK-LABEL: simulation.func private @unit_1
// TABLE-FALLBACK: simulation.random.solve

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property @s4.value attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
        }
        obelisk.sv.symbol.constraint_block @s5.even attributes {hierarchical_name = "C::even", name = "even", node_id = 5 : i64, this_variable_path = "C::even.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.even::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.binary_op attributes {node_id = 9 : i64, operator_kind = 4 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 27 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 28 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable @s6.this attributes {hierarchical_name = "C::even.this", is_compiler_generated, is_const, name = "this", node_id = 10 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.subroutine @s7.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s8.pre_randomize attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 13 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s9.post_randomize attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 15 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
        }
        obelisk.sv.symbol.variable @s10.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s11.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 18 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s12.top attributes {hierarchical_name = "top", name = "top", node_id = 19 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s13.object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s14 attributes {hierarchical_name = "top", node_id = 22 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s11.top::@s12.top} {
              obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s11.top::@s12.top::@s13.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}
