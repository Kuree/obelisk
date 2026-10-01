// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 vpi=read' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir OBELISK_TEST_ALIAS_SHAPE=1 \
// RUN:   %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.NetIdentityQueries

!bit = !obelisk.integral<1, false, true, 0 : 0, logic>
!down = !obelisk.ranged_packed_array<3 : 0 x !bit>
!up = !obelisk.ranged_packed_array<0 : 3 x !bit>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @top_i attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def
    } {
      obelisk.sv.symbol.instance_body @top_b attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64,
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.type.type_alias @up_t attributes {hierarchical_name = "top.up_t",
            name = "up_t", node_id = 4 : i64, semantic_type = !up,
            vpi_typedef_layers = [{aliases = [
              @root::@top_i::@top_b::@up_t], path = array<i64>}]} {}
        obelisk.sv.symbol.net @a attributes {hierarchical_name = "top.a",
            is_implicit = false, name = "a", net_kind = 1 : i32,
            node_id = 5 : i64, semantic_type = !down} {}
        obelisk.sv.symbol.net @b attributes {hierarchical_name = "top.b",
            is_implicit = false, name = "b", net_kind = 1 : i32,
            node_id = 6 : i64, semantic_type = !up,
            vpi_typedef_layers = [{aliases = [
              @root::@top_i::@top_b::@up_t], path = array<i64>}]} {}
        obelisk.sv.symbol.net_alias @alias attributes {hierarchical_name = "top",
            node_id = 7 : i64} {
          obelisk.sv.expression.named_value attributes {is_signed = false,
              node_id = 8 : i64, referenced_path = "top.a",
              referenced_symbol = @root::@top_i::@top_b::@a,
              semantic_type = !down} {}
          obelisk.sv.expression.named_value attributes {is_signed = false,
              node_id = 9 : i64, referenced_path = "top.b",
              referenced_symbol = @root::@top_i::@top_b::@b,
              semantic_type = !up} {}
        }
      }
    }
  }
}

// CHECK: simulation.vpi_typespec.decl @[[UP_T:[^ ]+]]
// CHECK-SAME: hierarchy "top.up_t"
// CHECK: simulation.net.decl [[A:[0-9]+]] {{.*}} : !simulation.packed_array<3 : 0 x !simulation.logic<1>>
// CHECK-SAME: hierarchy "top.a"
// CHECK: simulation.vpi_net_identity.decl [[B:[0-9]+]] backed_by [[A]] {{.*}} : !simulation.packed_array<0 : 3 x !simulation.logic<1>>
// CHECK-SAME: hierarchy "top.b"
// CHECK-SAME: vpi_type = #simulation.vpi_type<kind = packed_array
// CHECK-SAME: range = [0, 3]
// CHECK-SAME: typedefAliases = [@[[UP_T]]]
// CHECK: simulation.vpi_relation.decl <kind = net_identity, id = [[B]] : i64> selector 126 handle ordinal 0 to <kind = net, id = [[A]] : i64>
