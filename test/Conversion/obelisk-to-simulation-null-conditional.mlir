// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.11 gives `condition ? null : null` the literal-null
// type. Its enclosing assignment supplies the concrete class-handle type.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64, sym_name = "s0.m"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s4.this} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 4 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s4.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 5 : i64, referenced_path = "m", referenced_symbol = @s0.m, sym_name = "s5.m"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "m", name = "m", node_id = 6 : i64, sym_name = "s6.m", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "m.condition", lifetime = 1 : i32, name = "condition", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s7.condition"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "m.c", lifetime = 1 : i32, name = "c", node_id = 8 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s8.c"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "m", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "m.c", referenced_symbol = @s1.$root::@s5.m::@s6.m::@s8.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
              obelisk.sv.expression.conversion attributes {is_implicit = true, node_id = 13 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 14 : i64, semantic_type = !obelisk.null} {
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "m.condition", referenced_symbol = @s1.$root::@s5.m::@s6.m::@s7.condition, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.null_literal attributes {node_id = 16 : i64, semantic_type = !obelisk.null} {
                  }
                  obelisk.sv.expression.null_literal attributes {node_id = 17 : i64, semantic_type = !obelisk.null} {
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

// CHECK: %[[NULL:.*]] = obelisk_sim.class.null : !obelisk_sim.class_handle<@[[CLASS:__obelisk_class_[^>]+]]>
// CHECK: obelisk_sim.ref.store %[[NULL]] to %{{.*}} : !obelisk_sim.class_handle<@[[CLASS]]>
// CHECK-NOT: obelisk.sv.
