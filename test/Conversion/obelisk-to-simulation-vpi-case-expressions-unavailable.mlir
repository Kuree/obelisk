// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read opt-level=0' -o %t
// RUN: env OBELISK_TEST_UNAVAILABLE_CASE=1 OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CaseItemExpressions
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env OBELISK_TEST_UNAVAILABLE_CASE=1 OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CaseItemExpressions
!nibble = !obelisk.integral<4, false, false, 3 : 0, bit>
!pair = !obelisk.integral<2, false, false, 1 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @definition attributes {node_id = 1 : i64, name = "top", hierarchical_name = "top", definition_kind = 0 : i32} {}
  obelisk.sv.symbol.root @root attributes {node_id = 2 : i64, name = "$root", hierarchical_name = "$root"} {
    obelisk.sv.symbol.instance @top attributes {node_id = 3 : i64, name = "top", hierarchical_name = "top", referenced_symbol = @definition, referenced_path = "top", is_uninstantiated = false} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "top", hierarchical_name = "top"} {
        obelisk.sv.symbol.variable @a attributes {node_id = 5 : i64, name = "a", hierarchical_name = "top.a", lifetime = 1 : i32, semantic_type = !nibble} {}
        obelisk.sv.symbol.variable @selector attributes {node_id = 6 : i64, name = "selector", hierarchical_name = "top.selector", lifetime = 1 : i32, semantic_type = !nibble} {}
        obelisk.sv.symbol.parameter @P attributes {node_id = 7 : i64, name = "P", hierarchical_name = "top.P", constant_value = "9", semantic_type = !nibble} {}
        obelisk.sv.symbol.procedural_block @initial attributes {node_id = 8 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          obelisk.sv.statement.case attributes {node_id = 9 : i64, condition_kind = 0 : i32, check_kind = 0 : i32, item_count = 1 : i64, item_label_counts = array<i64: 6>, has_default = true} {
            obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_symbol = @root::@top::@body::@selector, referenced_path = "top.selector", semantic_type = !nibble} {}
            obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !nibble} {}
            obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !nibble} {}
            obelisk.sv.expression.integer_literal attributes {node_id = 13 : i64, constant_value = "1", is_signed = false, semantic_type = !nibble} {}
            obelisk.sv.expression.integer_literal attributes {node_id = 14 : i64, constant_value = "1", is_signed = false, semantic_type = !nibble} {}
            obelisk.sv.expression.concatenation attributes {node_id = 15 : i64, is_signed = false, semantic_type = !nibble} {
              obelisk.sv.expression.integer_literal attributes {node_id = 16 : i64, constant_value = "1", is_signed = false, semantic_type = !pair} {}
              obelisk.sv.expression.integer_literal attributes {node_id = 17 : i64, constant_value = "2", is_signed = false, semantic_type = !pair} {}
            }
            // This final label is outside the current materializer subset.
            // Earlier supported literals and concat must leave no partial group.
            obelisk.sv.expression.unary_op attributes {node_id = 21 : i64, operator_kind = 2 : i32, semantic_type = !nibble, is_signed = false} {
              obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_symbol = @root::@top::@body::@P, referenced_path = "top.P", semantic_type = !nibble} {}
            }
            obelisk.sv.statement.empty attributes {node_id = 19 : i64} {}
            obelisk.sv.statement.empty attributes {node_id = 20 : i64} {}
          }
        }
      }
    }
  }
}
