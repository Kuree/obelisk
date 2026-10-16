// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.GenericClassFields

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 0 : i64} {
    obelisk.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 2 : i64, is_interface = false, specialization_count = 0 : i64} {
        obelisk.sv.source.type_parameter @t attributes {hierarchical_name = "G::T", name = "T", node_id = 3 : i64} {}
        obelisk.sv.source.class_type_alias @word attributes {hierarchical_name = "G::Word", name = "Word", node_id = 4 : i64, member_visibility = 0 : i32} {
          obelisk.sv.source.declared_type attributes {node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.source.class_type_alias @count attributes {hierarchical_name = "G::Count", name = "Count", node_id = 6 : i64, member_visibility = 0 : i32} {
          obelisk.sv.source.declared_type_reference attributes {node_id = 7 : i64, declaration = @word} {}
        }
        obelisk.sv.source.class_property @x attributes {hierarchical_name = "G::x", name = "x", node_id = 8 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 1 : i32} {
          obelisk.sv.source.declared_type attributes {node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.source.class_property @escaped attributes {hierarchical_name = "G::E.p", name = "E.p", node_id = 10 : i64, lifetime = 1 : i32, member_visibility = 2 : i32, rand_mode = 0 : i32, is_const} {
          obelisk.sv.source.declared_type_reference attributes {node_id = 11 : i64, declaration = @count} {}
        }
        obelisk.sv.source.class_property @flag attributes {hierarchical_name = "G::flag", name = "flag", node_id = 12 : i64, lifetime = 0 : i32, member_visibility = 1 : i32, rand_mode = 0 : i32} {
          obelisk.sv.source.declared_type attributes {node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
        }
        obelisk.sv.source.class_property @wake attributes {hierarchical_name = "G::wake", name = "wake", node_id = 14 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {
          obelisk.sv.source.declared_type attributes {node_id = 15 : i64, semantic_type = !obelisk.event} {}
        }
        // T has no public variable kind before specialization. Preserve the
        // source reference without manufacturing a vpiReg or an effective type.
        obelisk.sv.source.class_property @dependent attributes {hierarchical_name = "G::dependent", name = "dependent", node_id = 16 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {
          obelisk.sv.source.declared_type_reference attributes {node_id = 17 : i64, declaration = @t} {}
        }
        obelisk.sv.source.class_method @static_method attributes {hierarchical_name = "G::static_method", name = "static_method", node_id = 18 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_static, source_virtuality = false} {}
        obelisk.sv.source.class_method @virtual_method attributes {hierarchical_name = "G::virtual_method", name = "virtual_method", node_id = 19 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_declared_virtual, source_virtuality = true} {}
        obelisk.sv.source.class_method @unknown_method attributes {hierarchical_name = "G::unknown_method", name = "unknown_method", node_id = 20 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32} {}
      }
    }
  }
}

// CHECK-NOT: simulation.class.decl
// CHECK-NOT: simulation.class.field
// CHECK-DAG: simulation.vpi_object.anchor @[[G:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "G"
// CHECK-DAG: simulation.vpi_typespec.decl @[[WORD:[^ ]+]] {{.*}}owner @[[G]] hierarchy "G::Word" debug "Word"
// CHECK-DAG: simulation.vpi_typespec.decl @[[COUNT:[^ ]+]] {{.*}}owner @[[G]] hierarchy "G::Count" debug "Count"
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[G]] hierarchy "G::x" debug "x"
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[G]] hierarchy "G::E.p" debug "E.p"
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[G]] hierarchy "G::flag" debug "flag"
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[G]] hierarchy "G::wake" debug "wake"
// CHECK-NOT: hierarchy "G::dependent"
