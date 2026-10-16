// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceTypeParameters

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 0 : i64} {
    obelisk.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      // The lexical declaration exists without any concrete specialization.
      obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 2 : i64,
          is_interface = false, specialization_count = 0 : i64} {
        obelisk.sv.source.type_parameter @t attributes {hierarchical_name = "G::T", name = "T", node_id = 3 : i64} {
          obelisk.sv.source.type_default attributes {node_id = 4 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.source.type_parameter @u attributes {hierarchical_name = "G::U", name = "U", node_id = 5 : i64} {
          obelisk.sv.source.type_reference_default attributes {node_id = 6 : i64, parameter = @t} {}
        }
        obelisk.sv.source.type_parameter @v attributes {hierarchical_name = "G::V", name = "V", node_id = 7 : i64} {}
        obelisk.sv.source.type_parameter @w attributes {hierarchical_name = "G::W", name = "W", node_id = 8 : i64, is_local_param} {
          obelisk.sv.source.unsupported_default attributes {node_id = 9 : i64, syntax = "T[3:0]", reason = "dependent packed dimension"} {}
        }
      }
    }
  }
}

// CHECK-DAG: simulation.vpi_object.anchor @[[G:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "G"
// CHECK-DAG: simulation.vpi_object.anchor @[[T:[^ ]+]] {{.*}}type 609 {{.*}}parent @[[G]] ordinal 0 hierarchy "G::T"{{.*}}unresolved_type_parameter
// CHECK-DAG: simulation.vpi_object.anchor @[[U:[^ ]+]] {{.*}}type 609 {{.*}}parent @[[G]] ordinal 1 hierarchy "G::U"{{.*}}unresolved_type_parameter
// CHECK-DAG: simulation.vpi_object.anchor @[[V:[^ ]+]] {{.*}}type 609 {{.*}}parent @[[G]] ordinal 2 hierarchy "G::V"{{.*}}unresolved_type_parameter
// CHECK-DAG: simulation.vpi_object.anchor @[[W:[^ ]+]] {{.*}}type 609 {{.*}}parent @[[G]] ordinal 3 hierarchy "G::W"{{.*}}unavailable_default = "T[3:0]", unresolved_type_parameter
// CHECK-DAG: simulation.vpi_typespec.decl @[[DEFAULT:[^ ]+]] {{.*}}owner @[[T]] hierarchy "" debug ""{{.*}}origin = 5 : i32{{.*}}kind = int
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[T]]> selector 102 handle ordinal 0 to <kind = symbol, symbol = @[[DEFAULT]]>
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[U]]> selector 102 handle ordinal 0 to <kind = symbol, symbol = @[[T]]>
