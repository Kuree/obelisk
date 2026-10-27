// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// Two 257-bit weights have an exact 258-bit total. The compiler determines
// this width and emits a complete sampler, with no runtime overflow failure.
// CHECK: %[[FIRST:.*]] = arith.extui {{.*}} : i257 to i258
// CHECK: %[[SECOND:.*]] = arith.extui {{.*}} : i257 to i258
// CHECK: %[[TOTAL:.*]] = arith.addi %[[FIRST]], %[[SECOND]] : i258
// CHECK: arith.subi %[[TOTAL]], {{.*}} : i258
// CHECK-COUNT-5: simulation.random.next
// CHECK: arith.andi {{.*}} : i258
// CHECK: arith.cmpi ult, {{.*}}, %[[TOTAL]] : i258

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 70512 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 70511 : i64} {
    talos.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 70510 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 70509 : i64} {
        talos.sv.symbol.variable @weight attributes {hierarchical_name = "top.weight", lifetime = 1 : i32, name = "weight", semantic_type = !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 70483 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 70482 : i64} {}
        }
        talos.sv.symbol.variable @other_weight attributes {hierarchical_name = "top.other_weight", lifetime = 1 : i32, name = "other_weight", semantic_type = !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 70485 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 70484 : i64} {}
        }
        talos.sv.symbol.variable @selected attributes {hierarchical_name = "top.selected", lifetime = 1 : i32, name = "selected", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 70487 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 70486 : i64} {}
        }
        talos.sv.symbol.statement_block @grammar_11 attributes {block_kind = 0 : i32, hierarchical_name = "top.grammar_11", node_id = 70505 : i64} {
          talos.sv.symbol.rand_seq_production @weighted_11 attributes {argument_count = 0 : i64, hierarchical_name = "top.grammar_11.main", name = "main", rule_count = 2 : i64, rule_has_rand_join_expressions = array<i64: 0, 0>, rule_has_weight_code_blocks = array<i64: 0, 0>, rule_has_weights = array<i64: 1, 1>, rule_is_rand_join = array<i64: 0, 0>, rule_item_counts = array<i64: 1, 1>, rule_blocks = [@root::@instance::@body::@grammar_11::@weighted_11::@rule_11_0, @root::@instance::@body::@grammar_11::@weighted_11::@rule_11_1], semantic_type = !talos.void, node_id = 70504 : i64} {
            talos.sv.symbol.statement_block @rule_11_0 attributes {block_kind = 0 : i32, hierarchical_name = "top.grammar_11.main.r0", node_id = 70498 : i64} {}
            talos.sv.expression.named_value attributes {referenced_path = "top.weight", referenced_symbol = @root::@instance::@body::@weight, semantic_type = !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 70488 : i64} {}
            talos.sv.rand_seq.code_block attributes {block = @root::@instance::@body::@grammar_11::@weighted_11::@rule_11_0, block_path = "top.grammar_11.main.r0", node_id = 70500 : i64} {
              talos.sv.statement.block attributes {node_id = 70499 : i64} {
                talos.sv.statement.expression_statement attributes {node_id = 70493 : i64} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 70492 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.selected", referenced_symbol = @root::@instance::@body::@selected, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 70491 : i64} {}
                    talos.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 70490 : i64} {}
                  }
                }
              }
            }
            talos.sv.symbol.statement_block @rule_11_1 attributes {block_kind = 0 : i32, hierarchical_name = "top.grammar_11.main.r1", node_id = 70501 : i64} {}
            talos.sv.expression.named_value attributes {referenced_path = "top.other_weight", referenced_symbol = @root::@instance::@body::@other_weight, semantic_type = !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 70489 : i64} {}
            talos.sv.rand_seq.code_block attributes {block = @root::@instance::@body::@grammar_11::@weighted_11::@rule_11_1, block_path = "top.grammar_11.main.r1", node_id = 70503 : i64} {
              talos.sv.statement.block attributes {node_id = 70502 : i64} {
                talos.sv.statement.expression_statement attributes {node_id = 70497 : i64} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 70496 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.selected", referenced_symbol = @root::@instance::@body::@selected, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 70495 : i64} {}
                    talos.sv.expression.integer_literal attributes {constant_value = "2", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 70494 : i64} {}
                  }
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 70508 : i64} {
          talos.sv.statement.list attributes {node_id = 70507 : i64} {
            talos.sv.statement.rand_sequence attributes {first_production = @root::@instance::@body::@grammar_11::@weighted_11, first_production_path = "top.grammar_11.main", has_first_production = true, production_count = 1 : i64, node_id = 70506 : i64} {}
          }
        }
      }
    }
  }
}
