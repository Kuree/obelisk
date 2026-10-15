// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 18.17.5: rand join interleaves the items of its operand
// productions, keeping each operand's own order, whatever their lengths.
// Each item is a step; the scheduler tracks each operand's next step and
// steps left, picks uniformly among operands with steps left (without a draw
// when only one is left), and runs the picked step through a switch.
//
//   module top;
//     int seen;
//     initial
//       randsequence(main)
//         main : rand join left right;
//         left : a b;
//         right : c;
//         a : { seen += 1; };
//         b : { seen += 2; };
//         c : { seen += 4; };
//       endsequence
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: ^[[SCHEDULER:bb[0-9]+]](%[[LEFT_NEXT:.*]]: i64, %[[LEFT_REMAINING:.*]]: i64, %[[RIGHT_NEXT:.*]]: i64, %[[RIGHT_REMAINING:.*]]: i64):
// CHECK: arith.cmpi sgt, %[[LEFT_REMAINING]],
// CHECK: arith.cmpi sgt, %[[RIGHT_REMAINING]],
// CHECK: %[[COUNT:.*]] = arith.addi
// CHECK: ^[[A:bb[0-9]+]]:
// CHECK: arith.constant {{.*}}1 : i32
// CHECK: %[[LEFT_AFTER_A:.*]] = arith.subi %[[LEFT_REMAINING]],
// CHECK: cf.br ^[[SCHEDULER]](%{{.*}}, %[[LEFT_AFTER_A]], %[[RIGHT_NEXT]], %[[RIGHT_REMAINING]] :
// CHECK: ^[[B:bb[0-9]+]]:
// CHECK: arith.constant {{.*}}2 : i32
// CHECK: cf.br ^[[SCHEDULER]](%{{.*}}, %{{.*}}, %[[RIGHT_NEXT]], %[[RIGHT_REMAINING]] :
// CHECK: ^[[C:bb[0-9]+]]:
// CHECK: arith.constant {{.*}}4 : i32
// CHECK: cf.br ^[[SCHEDULER]](%[[LEFT_NEXT]], %[[LEFT_REMAINING]], %{{.*}}, %{{.*}} :
// CHECK: %[[SINGLE:.*]] = arith.cmpi eq, %[[COUNT]],
// CHECK: cf.cond_br %[[SINGLE]], ^[[PICK:bb[0-9]+]](%{{.*}} : i64), ^[[DRAW:bb[0-9]+]]
// CHECK: ^[[DRAW]]:
// CHECK-NEXT: simulation.random.bounded %arg0, %[[COUNT]]
// CHECK: ^[[PICK]](%{{.*}}: i64):
// CHECK: cf.switch %{{.*}} : i64, [
// CHECK-NEXT: default: ^{{bb[0-9]+}},
// CHECK-NEXT: 0: ^[[A]],
// CHECK-NEXT: 1: ^[[B]],
// CHECK-NEXT: 2: ^[[C]]

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.seen attributes {hierarchical_name = "top.seen", lifetime = 1 : i32, name = "seen", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.statement_block @s6 attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 6 : i64} {
          obelisk.sv.symbol.rand_seq_production @s7.main attributes {argument_count = 0 : i64, hierarchical_name = "top.main", name = "main", node_id = 7 : i64, rule_blocks = [@s1.$root::@s3.top::@s4.top::@s6::@s7.main::@s8], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 1>, rule_item_counts = array<i64: 2>, semantic_type = !obelisk.void} {
            obelisk.sv.symbol.statement_block @s8 attributes {block_kind = 0 : i32, hierarchical_name = "top.main", node_id = 8 : i64} {
            }
            obelisk.sv.rand_seq.item attributes {argument_count = 0 : i64, has_target = true, node_id = 9 : i64, target = @s1.$root::@s3.top::@s4.top::@s6::@s9.left, target_path = "top.left"} {
            }
            obelisk.sv.rand_seq.item attributes {argument_count = 0 : i64, has_target = true, node_id = 10 : i64, target = @s1.$root::@s3.top::@s4.top::@s6::@s11.right, target_path = "top.right"} {
            }
          }
          obelisk.sv.symbol.rand_seq_production @s9.left attributes {argument_count = 0 : i64, hierarchical_name = "top.left", name = "left", node_id = 11 : i64, rule_blocks = [@s1.$root::@s3.top::@s4.top::@s6::@s9.left::@s10], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>, rule_item_counts = array<i64: 2>, semantic_type = !obelisk.void} {
            obelisk.sv.symbol.statement_block @s10 attributes {block_kind = 0 : i32, hierarchical_name = "top.left", node_id = 12 : i64} {
            }
            obelisk.sv.rand_seq.item attributes {argument_count = 0 : i64, has_target = true, node_id = 13 : i64, target = @s1.$root::@s3.top::@s4.top::@s6::@s13.a, target_path = "top.a"} {
            }
            obelisk.sv.rand_seq.item attributes {argument_count = 0 : i64, has_target = true, node_id = 14 : i64, target = @s1.$root::@s3.top::@s4.top::@s6::@s16.b, target_path = "top.b"} {
            }
          }
          obelisk.sv.symbol.rand_seq_production @s11.right attributes {argument_count = 0 : i64, hierarchical_name = "top.right", name = "right", node_id = 15 : i64, rule_blocks = [@s1.$root::@s3.top::@s4.top::@s6::@s11.right::@s12], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>, rule_item_counts = array<i64: 1>, semantic_type = !obelisk.void} {
            obelisk.sv.symbol.statement_block @s12 attributes {block_kind = 0 : i32, hierarchical_name = "top.right", node_id = 16 : i64} {
            }
            obelisk.sv.rand_seq.item attributes {argument_count = 0 : i64, has_target = true, node_id = 17 : i64, target = @s1.$root::@s3.top::@s4.top::@s6::@s19.c, target_path = "top.c"} {
            }
          }
          obelisk.sv.symbol.rand_seq_production @s13.a attributes {argument_count = 0 : i64, hierarchical_name = "top.a", name = "a", node_id = 18 : i64, rule_blocks = [@s1.$root::@s3.top::@s4.top::@s6::@s13.a::@s14], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>, rule_item_counts = array<i64: 1>, semantic_type = !obelisk.void} {
            obelisk.sv.symbol.statement_block @s14 attributes {block_kind = 0 : i32, hierarchical_name = "top.a", node_id = 19 : i64} {
              obelisk.sv.symbol.statement_block @s15 attributes {block_kind = 0 : i32, hierarchical_name = "top.a", node_id = 20 : i64} {
              }
            }
            obelisk.sv.rand_seq.code_block attributes {block = @s1.$root::@s3.top::@s4.top::@s6::@s13.a::@s14::@s15, block_path = "top.a", node_id = 21 : i64} {
              obelisk.sv.statement.block attributes {node_id = 22 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 24 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 25 : i64, referenced_path = "top.seen", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seen, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 26 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.l_value_reference attributes {is_signed = true, node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.rand_seq_production @s16.b attributes {argument_count = 0 : i64, hierarchical_name = "top.b", name = "b", node_id = 29 : i64, rule_blocks = [@s1.$root::@s3.top::@s4.top::@s6::@s16.b::@s17], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>, rule_item_counts = array<i64: 1>, semantic_type = !obelisk.void} {
            obelisk.sv.symbol.statement_block @s17 attributes {block_kind = 0 : i32, hierarchical_name = "top.b", node_id = 30 : i64} {
              obelisk.sv.symbol.statement_block @s18 attributes {block_kind = 0 : i32, hierarchical_name = "top.b", node_id = 31 : i64} {
              }
            }
            obelisk.sv.rand_seq.code_block attributes {block = @s1.$root::@s3.top::@s4.top::@s6::@s16.b::@s17::@s18, block_path = "top.b", node_id = 32 : i64} {
              obelisk.sv.statement.block attributes {node_id = 33 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 35 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 36 : i64, referenced_path = "top.seen", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seen, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 37 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.l_value_reference attributes {is_signed = true, node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.rand_seq_production @s19.c attributes {argument_count = 0 : i64, hierarchical_name = "top.c", name = "c", node_id = 40 : i64, rule_blocks = [@s1.$root::@s3.top::@s4.top::@s6::@s19.c::@s20], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>, rule_item_counts = array<i64: 1>, semantic_type = !obelisk.void} {
            obelisk.sv.symbol.statement_block @s20 attributes {block_kind = 0 : i32, hierarchical_name = "top.c", node_id = 41 : i64} {
              obelisk.sv.symbol.statement_block @s21 attributes {block_kind = 0 : i32, hierarchical_name = "top.c", node_id = 42 : i64} {
              }
            }
            obelisk.sv.rand_seq.code_block attributes {block = @s1.$root::@s3.top::@s4.top::@s6::@s19.c::@s20::@s21, block_path = "top.c", node_id = 43 : i64} {
              obelisk.sv.statement.block attributes {node_id = 44 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 45 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 46 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 47 : i64, referenced_path = "top.seen", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seen, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 48 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.l_value_reference attributes {is_signed = true, node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "4", is_declared_unsized = true, is_signed = true, node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s22 attributes {hierarchical_name = "top", node_id = 51 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 52 : i64} {
            obelisk.sv.statement.rand_sequence attributes {first_production = @s1.$root::@s3.top::@s4.top::@s6::@s7.main, first_production_path = "top.main", has_first_production = true, node_id = 53 : i64, production_count = 6 : i64} {
            }
          }
        }
      }
    }
  }
}

