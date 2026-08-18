// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 8.8: a class property declaration initializer is assigned to
// the property when the object is constructed, so the value is converted to
// the property's own type. `bit x = 1'b0` is the case that shows it: the
// literal is self-determined `bit [0:0]` while the property is a scalar
// `bit`, and the field reference has to name the property's type either way.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64, sym_name = "s0.m"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 1 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Cls", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Cls", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Cls>, sym_name = "s3.Cls", this_variable_path = "Cls::this", this_variable_symbol = @s1.$root::@s2::@s3.Cls::@s5.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Cls::x", name = "x", node_id = 4 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s4.x"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Cls::this", is_compiler_generated, is_const, name = "this", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Cls>, sym_name = "s5.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 7 : i64, referenced_path = "m", referenced_symbol = @s0.m, sym_name = "s6.m"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "m", name = "m", node_id = 8 : i64, sym_name = "s7.m", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "m.c", lifetime = 1 : i32, name = "c", node_id = 9 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Cls>, sym_name = "s8.c"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "m", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Cls>} {
              obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "m.c", referenced_symbol = @s1.$root::@s6.m::@s7.m::@s8.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Cls>} {
              }
              obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 14 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Cls>} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.class.field_ref %{{.*}}[@__obelisk_class_s3_Cls_field_0] : !obelisk_sim.class_handle<@__obelisk_class_s3_Cls> -> !obelisk_sim.managed_ref<i1, @__obelisk_class_s3_Cls>
