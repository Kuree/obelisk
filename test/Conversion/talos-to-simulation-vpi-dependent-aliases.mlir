// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.DependentTypeAliases

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @host attributes {definition_kind = 0 : i32, hierarchical_name = "host", name = "host", node_id = 12 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 0 : i64} {
    talos.sv.symbol.instance @host_instance attributes {hierarchical_name = "host", is_uninstantiated = false, name = "host", node_id = 13 : i64, referenced_path = "host", referenced_symbol = @host} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "host", name = "host", node_id = 14 : i64, vpi_scope_kind = 32 : i32, simulation.vpi_top = true, simulation.vpi_definition_name = "host", time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64} {}
    }
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 2 : i64, is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.type_parameter @t attributes {hierarchical_name = "G::T", name = "T", node_id = 3 : i64} {}
        talos.sv.source.class_type_alias @a attributes {hierarchical_name = "G::A", name = "A", node_id = 4 : i64, member_visibility = 0 : i32} {
          talos.sv.source.declared_type_reference attributes {node_id = 5 : i64, declaration = @t} {}
        }
        talos.sv.source.class_type_alias @b attributes {hierarchical_name = "G::B.alias", name = "B.alias", node_id = 6 : i64, member_visibility = 0 : i32} {
          talos.sv.source.declared_type_reference attributes {node_id = 7 : i64, declaration = @a} {}
        }
        talos.sv.source.class_type_alias @word attributes {hierarchical_name = "G::Word", name = "Word", node_id = 8 : i64, member_visibility = 0 : i32} {
          talos.sv.source.declared_type attributes {node_id = 9 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
        }
        talos.sv.source.class_property @dependent attributes {hierarchical_name = "G::dependent", name = "dependent", node_id = 10 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {
          talos.sv.source.declared_type_reference attributes {node_id = 11 : i64, declaration = @b} {}
        }
      }
    }
  }
}

// CHECK-NOT: simulation.class.decl
// CHECK-NOT: simulation.class.field
// CHECK-DAG: simulation.vpi_object.anchor @[[G:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "G"
// CHECK-DAG: simulation.vpi_object.anchor @[[T:[^ ]+]] {{.*}}type 609 {{.*}}parent @[[G]] {{.*}}hierarchy "G::T"{{.*}}unresolved_type_parameter
// CHECK-DAG: simulation.vpi_unresolved_type_alias.decl @[[A:[^ ]+]] {{.*}}owner @[[G]] alias @[[T]] hierarchy "G::A" debug "A"
// CHECK-DAG: simulation.vpi_unresolved_type_alias.decl @{{[^ ]+}} {{.*}}owner @[[G]] alias @[[A]] hierarchy "G::B.alias" debug "B.alias"
// CHECK-DAG: simulation.vpi_typespec.decl @{{[^ ]+}} {{.*}}owner @[[G]] hierarchy "G::Word" debug "Word"
// CHECK-NOT: hierarchy "G::dependent"
