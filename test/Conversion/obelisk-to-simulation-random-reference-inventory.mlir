// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s

module {
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.Base attributes {bitstream_width = 8 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Base", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
        obelisk.sv.symbol.class_property @s4.x attributes {hierarchical_name = "Base::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<8, false, false, 7 : 0, bit>} {
        }
      }
      obelisk.sv.type.class_type @s5.Derived attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, bitstream_width = 16 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Derived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Derived", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s5.Derived>} {
        obelisk.sv.symbol.transparent_member @s6.x attributes {hierarchical_name = "Derived::x", name = "x", node_id = 6 : i64} {
        }
        obelisk.sv.symbol.class_property @s7.y attributes {hierarchical_name = "Derived::y", name = "y", node_id = 7 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.integral<8, false, false, 7 : 0, bit>} {
        }
      }
    }
  }
}

// CHECK: simulation.class.decl @[[BASE:[^ ]+]] id 1
// CHECK-SAME: random_variable_references = [#simulation.random_variable_reference<target = @[[BASE_FIELD:[^>]+]]>]
// CHECK: simulation.class.field @[[BASE_FIELD]] of @[[BASE]]
// CHECK-SAME: simulation.random_variable_kind = 1 : i32
// CHECK: simulation.class.decl @[[DERIVED:[^ ]+]] id 2 extends @[[BASE]]
// CHECK-SAME: random_variable_references = [#simulation.random_variable_reference<target = @[[BASE_FIELD]]>, #simulation.random_variable_reference<target = @[[DERIVED_FIELD:[^>]+]]>]
// CHECK: simulation.class.field @[[DERIVED_FIELD]] of @[[DERIVED]]
// CHECK-SAME: simulation.random_variable_kind = 2 : i32
