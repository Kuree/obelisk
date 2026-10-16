// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.NestedLexicalClasses

// Nested declarations retain one lexical identity across enclosing generic
// specializations. A never-specialized nested declaration retains its concrete
// members without manufacturing a class type or dependent variable kind.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64, name = "$root", hierarchical_name = "$root"} {
    obelisk.sv.symbol.compilation_unit @cu attributes {node_id = 1 : i64, hierarchical_name = "$unit"} {
      obelisk.sv.symbol.generic_class_def @g attributes {node_id = 2 : i64, hierarchical_name = "G", name = "G", is_interface = false, specialization_count = 2 : i64} {
        obelisk.sv.source.type_parameter @t attributes {node_id = 3 : i64, name = "T", hierarchical_name = "G::T"} {}
        obelisk.sv.source.class_definition @inner attributes {node_id = 4 : i64, name = "Inner", hierarchical_name = "G::Inner", is_abstract = true, is_interface = false, is_final = false, is_parameterized = false} {
          obelisk.sv.source.class_property @value attributes {node_id = 5 : i64, name = "value", hierarchical_name = "G::Inner::value", lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {
            obelisk.sv.source.declared_type attributes {node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          }
          obelisk.sv.source.class_property @dependent attributes {node_id = 7 : i64, name = "dependent", hierarchical_name = "G::Inner::dependent", lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {
            obelisk.sv.source.unavailable_type attributes {node_id = 8 : i64, syntax = "T"} {}
          }
          obelisk.sv.source.class_method @method attributes {node_id = 9 : i64, name = "inspect", hierarchical_name = "G::Inner::inspect", subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, source_virtuality = false} {}
          obelisk.sv.source.class_definition @deep attributes {node_id = 10 : i64, name = "Deep", hierarchical_name = "G::Inner::Deep", is_abstract = false, is_interface = false, is_final = false, is_parameterized = true} {
            obelisk.sv.source.type_parameter @u attributes {node_id = 11 : i64, name = "U", hierarchical_name = "G::Inner::Deep::U"} {}
            obelisk.sv.source.class_type_alias @ua attributes {node_id = 12 : i64, name = "UA", hierarchical_name = "G::Inner::Deep::UA", member_visibility = 0 : i32} {
              obelisk.sv.source.declared_type_reference attributes {node_id = 13 : i64, declaration = @u} {}
            }
          }
        }
        obelisk.sv.source.class_definition @unused attributes {node_id = 14 : i64, name = "Unused", hierarchical_name = "G::Unused", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {
          obelisk.sv.source.class_property @unused_value attributes {node_id = 15 : i64, name = "value", hierarchical_name = "G::Unused::value", lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {
            obelisk.sv.source.declared_type attributes {node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          }
        }
        obelisk.sv.type.class_type @outer_a attributes {node_id = 17 : i64, name = "G", hierarchical_name = "G#(8)", semantic_type = !obelisk.class_handle<@root::@cu::@g::@outer_a>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false} {
          obelisk.sv.type.class_type @inner_a attributes {node_id = 18 : i64, name = "Inner", hierarchical_name = "G#(8)::Inner", semantic_type = !obelisk.class_handle<@root::@cu::@g::@outer_a::@inner_a>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = true, is_final = false, is_interface = false, is_uninstantiated = false, lexical_definition = @root::@cu::@g::@inner} {
            obelisk.sv.symbol.class_property @dependent_a attributes {node_id = 19 : i64, name = "dependent", hierarchical_name = "G#(8)::Inner::dependent", semantic_type = !obelisk.integral<8, false, false, 7 : 0, bit>} {}
          }
        }
        obelisk.sv.type.class_type @outer_b attributes {node_id = 20 : i64, name = "G", hierarchical_name = "G#(16)", semantic_type = !obelisk.class_handle<@root::@cu::@g::@outer_b>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false} {
          obelisk.sv.type.class_type @inner_b attributes {node_id = 21 : i64, name = "Inner", hierarchical_name = "G#(16)::Inner", semantic_type = !obelisk.class_handle<@root::@cu::@g::@outer_b::@inner_b>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = true, is_final = false, is_interface = false, is_uninstantiated = false, lexical_definition = @root::@cu::@g::@inner} {
            obelisk.sv.symbol.class_property @dependent_b attributes {node_id = 22 : i64, name = "dependent", hierarchical_name = "G#(16)::Inner::dependent", semantic_type = !obelisk.integral<16, false, false, 15 : 0, bit>} {}
          }
        }
      }
    }
  }
}

// CHECK-DAG: simulation.vpi_object.anchor @[[G:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "G"
// CHECK-DAG: simulation.vpi_object.anchor @[[INNER:[^ ]+]] {{.*}}type 652 {{.*}}parent @[[G]] {{.*}}hierarchy "G::Inner"
// CHECK-DAG: simulation.vpi_object.anchor @[[DEEP:[^ ]+]] {{.*}}type 652 {{.*}}parent @[[INNER]] {{.*}}hierarchy "G::Inner::Deep"
// CHECK-DAG: simulation.vpi_object.anchor @[[UNUSED:[^ ]+]] {{.*}}type 652 {{.*}}parent @[[G]] {{.*}}hierarchy "G::Unused"
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[INNER]] hierarchy "G::Inner::value"
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[UNUSED]] hierarchy "G::Unused::value"
// CHECK-DAG: simulation.vpi_typespec.decl @[[A:[^ ]+]] {{.*}}owner @[[INNER]] hierarchy "G#(8)::Inner" {{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_typespec.decl @[[B:[^ ]+]] {{.*}}owner @[[INNER]] hierarchy "G#(16)::Inner" {{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[A]] {{.*}}hierarchy "G#(8)::Inner::dependent"
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[B]] {{.*}}hierarchy "G#(16)::Inner::dependent"
// CHECK-NOT: hierarchy "G::Inner::dependent"
