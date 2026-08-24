// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s

// IEEE 1800-2017 11.11 and 18.5: a min:typ:max expression in a dynamic
// constraint contributes only its compilation-selected branch to the random
// constraint template.

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 0 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 1 : i64, sym_name = "unit"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 128 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 2 : i64, semantic_type = !obelisk.class_handle<@root::@unit::@class>, sym_name = "class"} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 3 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "value"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::min_value", name = "min_value", node_id = 4 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "min_value"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::typ_value", name = "typ_value", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "typ_value"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::max_value", name = "max_value", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "max_value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::selected", name = "selected", node_id = 7 : i64, sym_name = "selected"} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 8 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 9 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 10 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "C::value", referenced_symbol = @root::@unit::@class::@value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.min_typ_max attributes {node_id = 12 : i64, selected_index = 1 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "C::min_value", referenced_symbol = @root::@unit::@class::@min_value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "C::typ_value", referenced_symbol = @root::@unit::@class::@typ_value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "C::max_value", referenced_symbol = @root::@unit::@class::@max_value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: obelisk_sim.random.constraint_template
// CHECK: %[[VALUE:.*]] = obelisk_sim.random.constraint_value 0 : i32
// CHECK-NEXT: %[[TYP:.*]] = obelisk_sim.random.constraint_value 1 : i32
// CHECK-NOT: obelisk_sim.random.constraint_value 2
// CHECK: %[[EQUAL:.*]] = arith.cmpi eq, %[[VALUE]], %[[TYP]] : i32
// CHECK: obelisk_sim.random.hard_constraint %[[EQUAL]]
