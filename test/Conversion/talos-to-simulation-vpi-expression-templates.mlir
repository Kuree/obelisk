// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=0' --mlir-print-debuginfo -o %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CompactExpressionTemplates
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' --mlir-print-debuginfo -o %t.opt.mlir
// RUN: env TALOS_TEST_INPUT=%t.opt.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CompactExpressionTemplates
!bit = !talos.integral<1, false, false, 0 : 0, bit>
!int = !talos.integral<32, true, false, 31 : 0, int>
!word = !talos.ranged_packed_array<31 : 0 x !bit>
!half = !talos.ranged_packed_array<15 : 0 x !bit>
!byte = !talos.ranged_packed_array<7 : 0 x !bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @source attributes {node_id = 1 : i64, name = "source", hierarchical_name = "source", definition_kind = 0 : i32} {}
  talos.sv.symbol.root @root attributes {node_id = 2 : i64, name = "$root", hierarchical_name = "$root"} {
    talos.sv.symbol.instance @left attributes {node_id = 3 : i64, name = "left", hierarchical_name = "left", referenced_symbol = @source, referenced_path = "source", is_uninstantiated = false} {
      talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "source", hierarchical_name = "left"} {
        talos.sv.symbol.variable @bits attributes {node_id = 5 : i64, name = "bits", hierarchical_name = "left.bits", lifetime = 1 : i32, semantic_type = !word} {}
        talos.sv.symbol.variable @q attributes {node_id = 6 : i64, name = "q", hierarchical_name = "left.q", lifetime = 1 : i32, semantic_type = !half} {}
        talos.sv.symbol.procedural_block @initial attributes {node_id = 7 : i64, hierarchical_name = "left", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            talos.sv.expression.assignment attributes {node_id = 9 : i64, assignment_kind = 0 : i32, semantic_type = !half} {
              talos.sv.expression.named_value attributes {node_id = 10 : i64, referenced_symbol = @root::@left::@body::@q, referenced_path = "left.q", semantic_type = !half} {}
              talos.sv.expression.concatenation attributes {node_id = 11 : i64, semantic_type = !half, is_signed = false} {
                talos.sv.expression.range_select attributes {node_id = 12 : i64, selection_kind = 0 : i32, semantic_type = !byte, is_signed = false} {
                  talos.sv.expression.named_value attributes {node_id = 13 : i64, referenced_symbol = @root::@left::@body::@bits, referenced_path = "left.bits", semantic_type = !word} {}
                  talos.sv.expression.integer_literal attributes {node_id = 14 : i64, constant_value = "15", is_signed = true, semantic_type = !int} {} loc("shared_expression.sv":10:3)
                  talos.sv.expression.integer_literal attributes {node_id = 15 : i64, constant_value = "8", is_signed = true, semantic_type = !int} {} loc("shared_expression.sv":10:4)
                } loc("shared_expression.sv":10:2)
                talos.sv.expression.range_select attributes {node_id = 16 : i64, selection_kind = 0 : i32, semantic_type = !byte, is_signed = false} {
                  talos.sv.expression.named_value attributes {node_id = 17 : i64, referenced_symbol = @root::@left::@body::@bits, referenced_path = "left.bits", semantic_type = !word} {}
                  talos.sv.expression.integer_literal attributes {node_id = 18 : i64, constant_value = "7", is_signed = true, semantic_type = !int} {} loc("shared_expression.sv":10:6)
                  talos.sv.expression.integer_literal attributes {node_id = 19 : i64, constant_value = "0", is_signed = true, semantic_type = !int} {} loc("shared_expression.sv":10:7)
                } loc("shared_expression.sv":10:5)
              } loc("shared_expression.sv":10:1)
            }
          }
        }
      }
    }
    talos.sv.symbol.instance @right attributes {node_id = 20 : i64, name = "right", hierarchical_name = "right", referenced_symbol = @source, referenced_path = "source", is_uninstantiated = false} {
      talos.sv.symbol.instance_body @body attributes {node_id = 21 : i64, name = "source", hierarchical_name = "right"} {
        talos.sv.symbol.variable @bits attributes {node_id = 22 : i64, name = "bits", hierarchical_name = "right.bits", lifetime = 1 : i32, semantic_type = !word} {}
        talos.sv.symbol.variable @q attributes {node_id = 23 : i64, name = "q", hierarchical_name = "right.q", lifetime = 1 : i32, semantic_type = !half} {}
        talos.sv.symbol.procedural_block @initial attributes {node_id = 24 : i64, hierarchical_name = "right", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 25 : i64} {
            talos.sv.expression.assignment attributes {node_id = 26 : i64, assignment_kind = 0 : i32, semantic_type = !half} {
              talos.sv.expression.named_value attributes {node_id = 27 : i64, referenced_symbol = @root::@right::@body::@q, referenced_path = "right.q", semantic_type = !half} {}
              talos.sv.expression.concatenation attributes {node_id = 28 : i64, semantic_type = !half, is_signed = false} {
                talos.sv.expression.range_select attributes {node_id = 29 : i64, selection_kind = 0 : i32, semantic_type = !byte, is_signed = false} {
                  talos.sv.expression.named_value attributes {node_id = 30 : i64, referenced_symbol = @root::@right::@body::@bits, referenced_path = "right.bits", semantic_type = !word} {}
                  talos.sv.expression.integer_literal attributes {node_id = 31 : i64, constant_value = "15", is_signed = true, semantic_type = !int} {} loc("shared_expression.sv":10:3)
                  talos.sv.expression.integer_literal attributes {node_id = 32 : i64, constant_value = "8", is_signed = true, semantic_type = !int} {} loc("shared_expression.sv":10:4)
                } loc("shared_expression.sv":10:2)
                talos.sv.expression.range_select attributes {node_id = 33 : i64, selection_kind = 0 : i32, semantic_type = !byte, is_signed = false} {
                  talos.sv.expression.named_value attributes {node_id = 34 : i64, referenced_symbol = @root::@right::@body::@bits, referenced_path = "right.bits", semantic_type = !word} {}
                  talos.sv.expression.integer_literal attributes {node_id = 35 : i64, constant_value = "7", is_signed = true, semantic_type = !int} {} loc("shared_expression.sv":10:6)
                  talos.sv.expression.integer_literal attributes {node_id = 36 : i64, constant_value = "0", is_signed = true, semantic_type = !int} {} loc("shared_expression.sv":10:7)
                } loc("shared_expression.sv":10:5)
              } loc("shared_expression.sv":10:1)
            }
          }
        }
      }
    }
  }
}
