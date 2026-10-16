// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.PrimitiveTerminals
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o %t.o3.mlir
// RUN: env OBELISK_TEST_INPUT=%t.o3.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.PrimitiveTerminals
// RUN: env OBELISK_TEST_INPUT=%t.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourcePrimitiveDefinitions
// RUN: env OBELISK_TEST_INPUT=%t.o3.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourcePrimitiveDefinitions

!logic = !obelisk.integral<1, false, true, 0 : 0, logic>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {}
  obelisk.sv.symbol.definition @other_def attributes {definition_kind = 0 : i32, hierarchical_name = "other", name = "other", node_id = 2 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 3 : i64} {
    obelisk.sv.symbol.instance @top_i attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @top_def, node_id = 4 : i64} {
      obelisk.sv.symbol.instance_body @top_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64} {
      obelisk.sv.symbol.net @a attributes {hierarchical_name = "top.a", name = "a", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 6 : i64} {}
      obelisk.sv.symbol.net @b attributes {hierarchical_name = "top.b", name = "b", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 7 : i64} {}
      obelisk.sv.symbol.net @out attributes {hierarchical_name = "top.out", name = "out", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 8 : i64} {}
      obelisk.sv.symbol.net @multi0 attributes {hierarchical_name = "top.multi0", name = "multi0", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 9 : i64} {}
      obelisk.sv.symbol.net @multi1 attributes {hierarchical_name = "top.multi1", name = "multi1", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 10 : i64} {}
      obelisk.sv.symbol.net @x attributes {hierarchical_name = "top.x", name = "x", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 11 : i64} {}
      obelisk.sv.symbol.net @y attributes {hierarchical_name = "top.y", name = "y", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 12 : i64} {}
      obelisk.sv.symbol.net @uout attributes {hierarchical_name = "top.uout", name = "uout", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 13 : i64} {}
      obelisk.sv.symbol.primitive_instance @g attributes {hierarchical_name = "top.g", name = "g", primitive_name = "and", time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64, node_id = 14 : i64} {
        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 15 : i64} {
          obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top_i::@top_body::@out, semantic_type = !logic, is_signed = false, node_id = 16 : i64} {}
          obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 17 : i64} {}
        }
        obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_body::@a, semantic_type = !logic, is_signed = false, node_id = 18 : i64} {}
        obelisk.sv.expression.named_value attributes {referenced_path = "top.b", referenced_symbol = @root::@top_i::@top_body::@b, semantic_type = !logic, is_signed = false, node_id = 19 : i64} {}
      }
      obelisk.sv.symbol.primitive_instance @g2 attributes {hierarchical_name = "top.g2", name = "g2", primitive_name = "and", time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64, node_id = 20 : i64} {
        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 21 : i64} {
          obelisk.sv.expression.named_value attributes {referenced_path = "top.out", referenced_symbol = @root::@top_i::@top_body::@out, semantic_type = !logic, is_signed = false, node_id = 22 : i64} {}
          obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 23 : i64} {}
        }
        obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_body::@a, semantic_type = !logic, is_signed = false, node_id = 24 : i64} {}
        obelisk.sv.expression.named_value attributes {referenced_path = "top.b", referenced_symbol = @root::@top_i::@top_body::@b, semantic_type = !logic, is_signed = false, node_id = 25 : i64} {}
      }
      obelisk.sv.symbol.primitive_instance @multi attributes {hierarchical_name = "top.multi", name = "multi", primitive_name = "buf", time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64, node_id = 26 : i64} {
        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 27 : i64} {
          obelisk.sv.expression.named_value attributes {referenced_path = "top.multi0", referenced_symbol = @root::@top_i::@top_body::@multi0, semantic_type = !logic, is_signed = false, node_id = 28 : i64} {}
          obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 29 : i64} {}
        }
        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 30 : i64} {
          obelisk.sv.expression.named_value attributes {referenced_path = "top.multi1", referenced_symbol = @root::@top_i::@top_body::@multi1, semantic_type = !logic, is_signed = false, node_id = 31 : i64} {}
          obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 32 : i64} {}
        }
        obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_body::@a, semantic_type = !logic, is_signed = false, node_id = 33 : i64} {}
      }
      obelisk.sv.symbol.primitive_instance @sw attributes {hierarchical_name = "top.sw", name = "sw", primitive_name = "tranif1", time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64, node_id = 34 : i64} {
        obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@top_i::@top_body::@x, semantic_type = !logic, is_signed = false, node_id = 35 : i64} {}
        obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@top_i::@top_body::@y, semantic_type = !logic, is_signed = false, node_id = 36 : i64} {}
        obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_body::@a, semantic_type = !logic, is_signed = false, node_id = 37 : i64} {}
      }
      obelisk.sv.symbol.primitive_instance @u attributes {hierarchical_name = "top.u", name = "u", primitive_name = "myudp", time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64, node_id = 38 : i64, udp_metadata = {name = "myudp", port_names = ["out", "a"], port_directions = array<i64: 1, 0>, is_sequential = false, is_edge_sensitive = false, table_inputs = ["0", "1"], table_states = array<i64: 0, 0>, table_outputs = array<i64: 48, 49>, table_edges = array<i64: 0, 0>}} {
        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 39 : i64} {
          obelisk.sv.expression.named_value attributes {referenced_path = "top.uout", referenced_symbol = @root::@top_i::@top_body::@uout, semantic_type = !logic, is_signed = false, node_id = 40 : i64} {}
          obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 41 : i64} {}
        }
        obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_body::@a, semantic_type = !logic, is_signed = false, node_id = 42 : i64} {}
      }
      }
    }
    obelisk.sv.symbol.instance @other_i attributes {hierarchical_name = "other", name = "other", is_uninstantiated = false, referenced_path = "other", referenced_symbol = @other_def, node_id = 43 : i64} {
      obelisk.sv.symbol.instance_body @other_body attributes {hierarchical_name = "other", name = "other", node_id = 44 : i64} {
      obelisk.sv.symbol.net @remote attributes {hierarchical_name = "other.remote", name = "remote", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, node_id = 45 : i64} {}
      obelisk.sv.symbol.primitive_instance @g attributes {hierarchical_name = "other.g", name = "g", primitive_name = "buf", time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64, node_id = 46 : i64} {
        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !logic, is_signed = false, node_id = 47 : i64} {
          obelisk.sv.expression.named_value attributes {referenced_path = "other.remote", referenced_symbol = @root::@other_i::@other_body::@remote, semantic_type = !logic, is_signed = false, node_id = 48 : i64} {}
          obelisk.sv.expression.empty_argument attributes {semantic_type = !logic, node_id = 49 : i64} {}
        }
        obelisk.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_body::@a, semantic_type = !logic, is_signed = false, node_id = 50 : i64} {}
      }
      }
    }
  }
}
