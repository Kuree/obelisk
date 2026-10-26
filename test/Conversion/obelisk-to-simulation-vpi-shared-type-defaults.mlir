// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read opt-level=0' --mlir-print-debuginfo -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SharedTypeDefaults:GeneratedDesignDatabase.SharedTypeDefaultImageSize
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=3' --mlir-print-debuginfo -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SharedTypeDefaults:GeneratedDesignDatabase.SharedTypeDefaultImageSize
// RUN: obelisk-opt %t.opt --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.llvm
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!byte = !obelisk.integral<8, false, false, 7 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @cell attributes {node_id = 1 : i64, definition_kind = 0 : i32, hierarchical_name = "cell", name = "cell"} {}
  obelisk.sv.symbol.root @root attributes {node_id = 2 : i64, hierarchical_name = "$root", name = "$root"} {
    obelisk.sv.symbol.instance @left attributes {node_id = 3 : i64, hierarchical_name = "left", is_uninstantiated = false, name = "left", referenced_path = "cell", referenced_symbol = @cell} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 4 : i64, hierarchical_name = "left", name = "cell"} {
        obelisk.sv.symbol.type_parameter @T attributes {node_id = 5 : i64, hierarchical_name = "left.T", name = "T", semantic_type = !byte} {
          obelisk.sv.source.type_default attributes {node_id = 6 : i64, semantic_type = !int} {} loc("original-types.sv":30:7)
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @U attributes {node_id = 7 : i64, hierarchical_name = "left.U", name = "U", semantic_type = !obelisk.string} {
          obelisk.sv.source.type_reference_default attributes {node_id = 8 : i64, parameter = @T} {}
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @V attributes {node_id = 9 : i64, hierarchical_name = "left.V", name = "V", semantic_type = !int} {
          obelisk.sv.source.type_reference_default attributes {node_id = 10 : i64, parameter = @U} {}
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @S attributes {node_id = 11 : i64, hierarchical_name = "left.S", name = "S", semantic_type = !obelisk.chandle} {
          obelisk.sv.source.type_default attributes {node_id = 12 : i64, semantic_type = !obelisk.string} {} loc("original-types.sv":40:7)
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @Absent attributes {node_id = 13 : i64, hierarchical_name = "left.Absent", name = "Absent", semantic_type = !int} {
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @Unsupported attributes {node_id = 14 : i64, hierarchical_name = "left.Unsupported", name = "Unsupported", semantic_type = !int} {
          obelisk.sv.source.unsupported_default attributes {node_id = 15 : i64, syntax = "some_alias", reason = "exact source alias unavailable"} {}
        } loc("original-types.sv":10:3)
      }
    }
    obelisk.sv.symbol.instance @right attributes {node_id = 16 : i64, hierarchical_name = "right", is_uninstantiated = false, name = "right", referenced_path = "cell", referenced_symbol = @cell} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 17 : i64, hierarchical_name = "right", name = "cell"} {
        obelisk.sv.symbol.type_parameter @T attributes {node_id = 18 : i64, hierarchical_name = "right.T", name = "T", semantic_type = !int} {
          obelisk.sv.source.type_default attributes {node_id = 19 : i64, semantic_type = !int} {} loc("original-types.sv":30:7)
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @U attributes {node_id = 20 : i64, hierarchical_name = "right.U", name = "U", semantic_type = !obelisk.chandle} {
          obelisk.sv.source.type_reference_default attributes {node_id = 21 : i64, parameter = @T} {}
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @V attributes {node_id = 22 : i64, hierarchical_name = "right.V", name = "V", semantic_type = !int} {
          obelisk.sv.source.type_reference_default attributes {node_id = 23 : i64, parameter = @U} {}
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @S attributes {node_id = 24 : i64, hierarchical_name = "right.S", name = "S", semantic_type = !obelisk.string} {
          obelisk.sv.source.type_default attributes {node_id = 25 : i64, semantic_type = !obelisk.string} {} loc("original-types.sv":40:7)
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @Absent attributes {node_id = 26 : i64, hierarchical_name = "right.Absent", name = "Absent", semantic_type = !int} {
        } loc("original-types.sv":10:3)
        obelisk.sv.symbol.type_parameter @Unsupported attributes {node_id = 27 : i64, hierarchical_name = "right.Unsupported", name = "Unsupported", semantic_type = !int} {
          obelisk.sv.source.unsupported_default attributes {node_id = 28 : i64, syntax = "some_alias", reason = "exact source alias unavailable"} {}
        } loc("original-types.sv":10:3)
      }
    }
  }
}
