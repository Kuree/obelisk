// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ClassParameters

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 0 : i64} {
    obelisk.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      obelisk.sv.type.type_alias @word attributes {hierarchical_name = "$unit::word", name = "word", node_id = 2 : i64,
          semantic_type = !obelisk.integral<8, false, false, 7 : 0, bit>,
          vpi_typedef_layers = [{aliases = [@root::@cu::@word], path = array<i64>}]} {}
      obelisk.sv.type.class_type @c attributes {bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "C",
          node_id = 3 : i64, semantic_type = !obelisk.class_handle<@root::@cu::@c>} {
        obelisk.sv.symbol.parameter @p attributes {constant_value = "7", hierarchical_name = "C::P", is_local_param,
            name = "P", node_id = 4 : i64, semantic_type = !obelisk.integral<8, false, false, 7 : 0, bit>,
            vpi_typedef_layers = [{aliases = [@root::@cu::@word], path = array<i64>}]} {}
        obelisk.sv.symbol.type_parameter @t attributes {hierarchical_name = "C::T", is_local_param,
            name = "T", node_id = 5 : i64, semantic_type = !obelisk.integral<8, false, false, 7 : 0, bit>,
            vpi_typedef_layers = [{aliases = [@root::@cu::@word], path = array<i64>}]} {}
      }
      obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 6 : i64,
          is_interface = false, specialization_count = 2 : i64} {
        obelisk.sv.type.class_type @g_int attributes {bitstream_width = 0 : i64,
            declared_interfaces = [], generic_parameter_paths = ["G#(int)::T"], generic_parameter_symbols = [@root::@cu::@g::@g_int::@t],
            has_base_constructor_call = false, has_cycles = false, hierarchical_name = "G#(int)",
            implemented_interfaces = [], is_abstract = false, is_final = false,
            is_interface = false, is_uninstantiated = false, name = "G", node_id = 7 : i64,
            semantic_type = !obelisk.class_handle<@root::@cu::@g::@g_int>} {
          obelisk.sv.symbol.type_parameter @t attributes {hierarchical_name = "G#(int)::T", name = "T", node_id = 8 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.type.class_type @g_bit attributes {bitstream_width = 0 : i64,
            declared_interfaces = [], generic_parameter_paths = ["G#(bit)::T"], generic_parameter_symbols = [@root::@cu::@g::@g_bit::@t],
            has_base_constructor_call = false, has_cycles = false, hierarchical_name = "G#(bit)",
            implemented_interfaces = [], is_abstract = false, is_final = false,
            is_interface = false, is_uninstantiated = false, name = "G", node_id = 9 : i64,
            semantic_type = !obelisk.class_handle<@root::@cu::@g::@g_bit>} {
          obelisk.sv.symbol.type_parameter @t attributes {hierarchical_name = "G#(bit)::T", name = "T", node_id = 10 : i64,
              semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
        }
      }
    }
  }
}

// CHECK-DAG: simulation.vpi_object.anchor @[[C:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "C"
// CHECK-DAG: simulation.vpi_typespec.decl @[[CT:[^ ]+]] {{.*}}owner @[[C]] hierarchy "C"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_typespec.decl @[[WORD:[^ ]+]] {{.*}}hierarchy "$unit::word"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 41 {{.*}}parent @[[C]] {{.*}}hierarchy "C::P"{{.*}}immutable_value = #simulation.frozen_constant<value = [7 : i8, 0 : i8]{{.*}}typedefAliases = [@[[WORD]]]
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 609 {{.*}}parent @[[C]] {{.*}}hierarchy "C::T"{{.*}}selector = 70 : i32, value = true{{.*}}typedefAliases = [@[[WORD]]]
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 41 {{.*}}parent @[[CT]] {{.*}}hierarchy "C::P"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 609 {{.*}}parent @[[CT]] {{.*}}hierarchy "C::T"
// CHECK-DAG: simulation.vpi_typespec.decl @[[GI:[^ ]+]] {{.*}}hierarchy "G#(int)"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_typespec.decl @[[GB:[^ ]+]] {{.*}}hierarchy "G#(bit)"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 609 {{.*}}parent @[[GI]] {{.*}}hierarchy "G#(int)::T"{{.*}}vpi_type = #simulation.vpi_type<kind = int
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 609 {{.*}}parent @[[GB]] {{.*}}hierarchy "G#(bit)::T"{{.*}}vpi_type = #simulation.vpi_type<kind = bit
