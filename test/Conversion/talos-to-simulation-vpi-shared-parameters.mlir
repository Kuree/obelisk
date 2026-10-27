// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read opt-level=0' -o %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SharedInstanceParameters
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env TALOS_TEST_INPUT=%t.opt %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SharedInstanceParameters
!int = !talos.integral<32, true, false, 31 : 0, int>
!byte = !talos.integral<8, false, false, 7 : 0, bit>
!bit = !talos.integral<1, false, false, 0 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @cell attributes {node_id = 1 : i64, definition_kind = 0 : i32, hierarchical_name = "cell", name = "cell"} {}
  talos.sv.symbol.definition @bus_def attributes {node_id = 2 : i64, definition_kind = 1 : i32, hierarchical_name = "bus", name = "bus"} {}
  talos.sv.symbol.root @root attributes {node_id = 3 : i64, hierarchical_name = "$root", name = "$root"} {
    talos.sv.symbol.package @pkg attributes {node_id = 100 : i64, hierarchical_name = "pkg", name = "pkg"} {
      talos.sv.type.type_alias @Word attributes {node_id = 101 : i64, hierarchical_name = "pkg::Word", name = "Word", semantic_type = !int, vpi_typedef_layers = [{aliases = [@root::@pkg::@Word], path = array<i64>}]} {}
    }
    talos.sv.symbol.instance @left attributes {node_id = 4 : i64, hierarchical_name = "left", is_uninstantiated = false, name = "left", referenced_path = "cell", referenced_symbol = @cell} {
      talos.sv.symbol.instance_body @body attributes {node_id = 5 : i64, hierarchical_name = "left", name = "cell"} {
        talos.sv.symbol.parameter @N attributes {node_id = 6 : i64, hierarchical_name = "left.N", name = "N", constant_value = "4", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 7 : i64} {
            talos.sv.expression.integer_literal attributes {node_id = 8 : i64, constant_value = "3", is_signed = true, semantic_type = !int} {}
          }
        }
        talos.sv.symbol.port @port attributes {node_id = 9 : i64, hierarchical_name = "left.port", name = "port", direction = 0 : i32, semantic_type = !bit} {}
        talos.sv.symbol.parameter @L attributes {node_id = 10 : i64, hierarchical_name = "left.L.p", name = "L.p", constant_value = "8'hfe", semantic_type = !talos.ranged_packed_array<7 : 0 x !bit>, is_local_param, has_explicit_range} {}
        talos.sv.symbol.type_parameter @T attributes {node_id = 11 : i64, hierarchical_name = "left.T", name = "T", semantic_type = !int, vpi_typedef_layers = [{aliases = [@root::@pkg::@Word], path = array<i64>}]} {
          talos.sv.source.type_default attributes {node_id = 12 : i64, semantic_type = !int} {}
        }
        talos.sv.symbol.parameter @omitted attributes {node_id = 13 : i64, hierarchical_name = "left.omitted", name = "omitted", constant_value = "7", semantic_type = !int} {}
        // A source use must remain compilable before contextual expression roots are enabled.
        talos.sv.symbol.procedural_block @initial attributes {node_id = 200 : i64, hierarchical_name = "left", procedure_kind = 0 : i32} {
          talos.sv.statement.conditional attributes {node_id = 201 : i64, condition_count = 1 : i64, has_else = false, check_kind = 0 : i32, condition_pattern_flags = array<i64: 0>} {
            talos.sv.expression.named_value attributes {node_id = 202 : i64, referenced_symbol = @root::@left::@body::@N, referenced_path = "left.N", semantic_type = !int} {}
            talos.sv.statement.empty attributes {node_id = 203 : i64} {}
          }
        }
      }
    }
    talos.sv.symbol.instance @right attributes {node_id = 14 : i64, hierarchical_name = "right", is_uninstantiated = false, name = "right", referenced_path = "cell", referenced_symbol = @cell} {
      talos.sv.symbol.instance_body @body attributes {node_id = 15 : i64, hierarchical_name = "right", name = "cell"} {
        talos.sv.symbol.parameter @N attributes {node_id = 16 : i64, hierarchical_name = "right.N", name = "N", constant_value = "4", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 17 : i64} {
            talos.sv.expression.integer_literal attributes {node_id = 18 : i64, constant_value = "3", is_signed = true, semantic_type = !int} {}
          }
        }
        talos.sv.symbol.port @port attributes {node_id = 19 : i64, hierarchical_name = "right.port", name = "port", direction = 0 : i32, semantic_type = !bit} {}
        talos.sv.symbol.parameter @L attributes {node_id = 20 : i64, hierarchical_name = "right.L.p", name = "L.p", constant_value = "8'hfe", semantic_type = !talos.ranged_packed_array<7 : 0 x !bit>, is_local_param, has_explicit_range} {}
        talos.sv.symbol.type_parameter @T attributes {node_id = 21 : i64, hierarchical_name = "right.T", name = "T", semantic_type = !int, vpi_typedef_layers = [{aliases = [@root::@pkg::@Word], path = array<i64>}]} {
          talos.sv.source.type_default attributes {node_id = 22 : i64, semantic_type = !int} {}
        }
        talos.sv.symbol.parameter @omitted attributes {node_id = 23 : i64, hierarchical_name = "right.omitted", name = "omitted", constant_value = "7", semantic_type = !int} {}
      }
    }
    talos.sv.symbol.instance @different attributes {node_id = 24 : i64, hierarchical_name = "different", is_uninstantiated = false, name = "different", referenced_path = "cell", referenced_symbol = @cell} {
      talos.sv.symbol.instance_body @body attributes {node_id = 25 : i64, hierarchical_name = "different", name = "cell"} {
        talos.sv.symbol.parameter @N attributes {node_id = 26 : i64, hierarchical_name = "different.N", name = "N", constant_value = "5", semantic_type = !int} {
          talos.sv.source.value_default attributes {node_id = 27 : i64} {
            talos.sv.expression.integer_literal attributes {node_id = 28 : i64, constant_value = "3", is_signed = true, semantic_type = !int} {}
          }
        }
        talos.sv.symbol.port @port attributes {node_id = 29 : i64, hierarchical_name = "different.port", name = "port", direction = 0 : i32, semantic_type = !bit} {}
        talos.sv.symbol.parameter @L attributes {node_id = 30 : i64, hierarchical_name = "different.L.p", name = "L.p", constant_value = "8'hfe", semantic_type = !talos.ranged_packed_array<7 : 0 x !bit>, is_local_param, has_explicit_range} {}
        talos.sv.symbol.type_parameter @T attributes {node_id = 31 : i64, hierarchical_name = "different.T", name = "T", semantic_type = !byte} {
          talos.sv.source.type_default attributes {node_id = 32 : i64, semantic_type = !int} {}
        }
        talos.sv.symbol.parameter @omitted attributes {node_id = 33 : i64, hierarchical_name = "different.omitted", name = "omitted", constant_value = "text", semantic_type = !talos.string} {}
      }
    }
    talos.sv.symbol.instance @bus attributes {node_id = 34 : i64, hierarchical_name = "bus", is_uninstantiated = false, name = "bus", referenced_path = "bus", referenced_symbol = @bus_def} {
      talos.sv.symbol.instance_body @body attributes {node_id = 35 : i64, hierarchical_name = "bus", name = "bus"} {
        talos.sv.symbol.parameter @K attributes {node_id = 36 : i64, hierarchical_name = "bus.K", name = "K", constant_value = "4", semantic_type = !int} {}
      }
    }
  }
}
