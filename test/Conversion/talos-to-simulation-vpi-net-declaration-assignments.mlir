// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=0' -o %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.NetDeclarationAssignments
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env TALOS_TEST_INPUT=%t.opt %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.NetDeclarationAssignments
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=off opt-level=3' -o /dev/null
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=0' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
!logic = !talos.integral<1, false, true, 0 : 0, logic>
!int = !talos.integral<32, true, false, 31 : 0, int>
!word = !talos.ranged_packed_array<7 : 0 x !logic>
!half = !talos.ranged_packed_array<3 : 0 x !logic>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @Top attributes {node_id = 100 : i64, definition_kind = 0 : i32, hierarchical_name = "Top", name = "Top"} {}
  talos.sv.symbol.root @root attributes {node_id = 101 : i64, hierarchical_name = "$root", name = "$root"} {
    talos.sv.symbol.instance @top attributes {node_id = 102 : i64, hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "Top", referenced_symbol = @Top} {
      talos.sv.symbol.instance_body @body attributes {node_id = 103 : i64, hierarchical_name = "top", name = "Top", time_unit_fs = 1 : i64, time_precision_fs = 1 : i64} {
        talos.sv.symbol.variable @source attributes {node_id = 1 : i64, hierarchical_name = "top.source", name = "source", lifetime = 1 : i32, semantic_type = !word} {}
        talos.sv.symbol.net @input attributes {node_id = 2 : i64, hierarchical_name = "top.input", name = "input", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic} {}
        talos.sv.symbol.net @out attributes {node_id = 3 : i64, hierarchical_name = "top.out", name = "out", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, drive_strength0 = 3 : i32, drive_strength1 = 2 : i32} {
          talos.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.input", referenced_symbol = @root::@top::@body::@input, semantic_type = !logic} {}
        }
        talos.sv.symbol.net @slice attributes {node_id = 5 : i64, hierarchical_name = "top.slice", name = "slice", net_kind = 1 : i32, is_implicit = false, semantic_type = !half} {
          talos.sv.expression.range_select attributes {node_id = 6 : i64, selection_kind = 0 : i32, semantic_type = !half, is_signed = false} {
            talos.sv.expression.named_value attributes {node_id = 7 : i64, referenced_path = "top.source", referenced_symbol = @root::@top::@body::@source, semantic_type = !word} {}
            talos.sv.expression.integer_literal attributes {node_id = 8 : i64, constant_value = "7", semantic_type = !int, is_signed = true} {}
            talos.sv.expression.integer_literal attributes {node_id = 9 : i64, constant_value = "4", semantic_type = !int, is_signed = true} {}
          }
        }
        talos.sv.symbol.net @delayed attributes {node_id = 10 : i64, hierarchical_name = "top.delayed", name = "delayed", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic, delay_fs = array<i64: 2, 5>} {
          talos.sv.expression.integer_literal attributes {node_id = 11 : i64, constant_value = "1'b1", semantic_type = !logic, is_signed = false} {}
          talos.sv.timing.delay3 attributes {delay_count = 2 : i64, node_id = 12 : i64} {
            talos.sv.expression.integer_literal attributes {node_id = 13 : i64, constant_value = "2", semantic_type = !int, is_signed = true} {}
            talos.sv.expression.integer_literal attributes {node_id = 14 : i64, constant_value = "5", semantic_type = !int, is_signed = true} {}
          }
        }
        talos.sv.symbol.continuous_assign @explicit attributes {node_id = 15 : i64, hierarchical_name = "top"} {
          talos.sv.expression.assignment attributes {node_id = 16 : i64, assignment_kind = 0 : i32, semantic_type = !logic} {
            talos.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.out", referenced_symbol = @root::@top::@body::@out, semantic_type = !logic} {}
            talos.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.input", referenced_symbol = @root::@top::@body::@input, semantic_type = !logic} {}
          }
        }
        talos.sv.symbol.generate_block @chosen attributes {node_id = 19 : i64, hierarchical_name = "top.chosen", name = "chosen"} {
          talos.sv.symbol.net @inner attributes {node_id = 20 : i64, hierarchical_name = "top.chosen.inner", name = "inner", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic} {
            talos.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.input", referenced_symbol = @root::@top::@body::@input, semantic_type = !logic} {}
          }
        }
        talos.sv.symbol.generate_block @inactive attributes {node_id = 22 : i64, hierarchical_name = "top.inactive", name = "inactive", is_uninstantiated = true} {
          talos.sv.symbol.net @hidden attributes {node_id = 23 : i64, hierarchical_name = "top.inactive.hidden", name = "hidden", net_kind = 1 : i32, is_implicit = false, semantic_type = !logic} {
            talos.sv.expression.integer_literal attributes {node_id = 24 : i64, constant_value = "1'b0", semantic_type = !logic, is_signed = false} {}
          }
        }
      }
    }
  }
}
