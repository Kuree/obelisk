// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 18.17: a production may call itself. A recursive production
// is emitted once; each call pushes the index of its resume block and the
// body's exit returns to the index it pops. A call that reenters the
// caller's own body saves that activation's state, here the argument k, and
// restores it when the call returns.
//
//   module top;
//     int n, last;
//     initial
//       randsequence(main)
//         main : r(0);
//         void r(int k) : { n--; } if (n > 0) r(k + 1) { last = k; };
//       endsequence
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[RETURNS:.*]] = simulation.container.create {{.*}}!simulation.queue<i64, 0>
// CHECK: %[[SAVED:.*]] = simulation.container.create {{.*}}!simulation.queue<i32, 0>
// CHECK: simulation.queue.insert %{{.*}} into %[[RETURNS]]
// CHECK: ^[[BODY:bb[0-9]+]](%[[K:.*]]: i32):
// CHECK: %[[NEXT:.*]] = arith.addi %[[K]],
// CHECK: simulation.queue.insert %[[K]] into %[[SAVED]]
// CHECK: simulation.queue.insert %{{.*}} into %[[RETURNS]]
// CHECK: cf.br ^[[BODY]](%[[NEXT]] : i32)
// CHECK: ^[[EXIT:bb[0-9]+]](%[[K_EXIT:.*]]: i32):
// CHECK-NEXT: simulation.ref.store %[[K_EXIT]] to %arg2
// CHECK: %[[INDEX:.*]] = simulation.container.read %[[RETURNS]]
// CHECK: cf.switch %[[INDEX]] : i64, [
// CHECK-NEXT: default: ^{{bb[0-9]+}},
// CHECK-NEXT: 1: ^[[RESUME:bb[0-9]+]]
// CHECK: ^[[RESUME]]:
// CHECK: %[[RESTORED:.*]] = simulation.container.read %[[SAVED]]
// CHECK: cf.br ^[[EXIT]](%[[RESTORED]] : i32)

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.n attributes {hierarchical_name = "top.n", lifetime = 1 : i32, name = "n", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s6.last attributes {hierarchical_name = "top.last", lifetime = 1 : i32, name = "last", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.statement_block @s7 attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 7 : i64} {
          obelisk.sv.symbol.rand_seq_production @s8.main attributes {argument_count = 0 : i64, hierarchical_name = "top.main", name = "main", node_id = 8 : i64, rule_blocks = [@s1.$root::@s3.top::@s4.top::@s7::@s8.main::@s9], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>, rule_item_counts = array<i64: 1>, semantic_type = !obelisk.void} {
            obelisk.sv.symbol.statement_block @s9 attributes {block_kind = 0 : i32, hierarchical_name = "top.main", node_id = 9 : i64} {
            }
            obelisk.sv.rand_seq.item attributes {argument_count = 1 : i64, has_target = true, node_id = 10 : i64, target = @s1.$root::@s3.top::@s4.top::@s7::@s10.r, target_path = "top.r"} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.rand_seq_production @s10.r attributes {argument_count = 1 : i64, hierarchical_name = "top.r", name = "r", node_id = 12 : i64, rule_blocks = [@s1.$root::@s3.top::@s4.top::@s7::@s10.r::@s12], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>, rule_item_counts = array<i64: 3>, semantic_type = !obelisk.void} {
            obelisk.sv.symbol.formal_argument @s11.k attributes {direction = 0 : i32, hierarchical_name = "top.r.k", name = "k", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
            obelisk.sv.symbol.statement_block @s12 attributes {block_kind = 0 : i32, hierarchical_name = "top.r", node_id = 14 : i64} {
              obelisk.sv.symbol.statement_block @s13 attributes {block_kind = 0 : i32, hierarchical_name = "top.r", node_id = 15 : i64} {
              }
              obelisk.sv.symbol.statement_block @s14 attributes {block_kind = 0 : i32, hierarchical_name = "top.r", node_id = 16 : i64} {
              }
            }
            obelisk.sv.rand_seq.code_block attributes {block = @s1.$root::@s3.top::@s4.top::@s7::@s10.r::@s12::@s13, block_path = "top.r", node_id = 17 : i64} {
              obelisk.sv.statement.block attributes {node_id = 18 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                  obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 20 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 21 : i64, referenced_path = "top.n", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.n, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.rand_seq.if_else attributes {has_else = false, node_id = 22 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 23 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 24 : i64, referenced_path = "top.n", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.n, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.rand_seq.item attributes {argument_count = 1 : i64, has_target = true, node_id = 26 : i64, target = @s1.$root::@s3.top::@s4.top::@s7::@s10.r, target_path = "top.r"} {
                obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 27 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 28 : i64, referenced_path = "top.r.k", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7::@s10.r::@s11.k, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.rand_seq.code_block attributes {block = @s1.$root::@s3.top::@s4.top::@s7::@s10.r::@s12::@s14, block_path = "top.r", node_id = 30 : i64} {
              obelisk.sv.statement.block attributes {node_id = 31 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 33 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 34 : i64, referenced_path = "top.last", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.last, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 35 : i64, referenced_path = "top.r.k", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7::@s10.r::@s11.k, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s15 attributes {hierarchical_name = "top", node_id = 36 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 37 : i64} {
            obelisk.sv.statement.rand_sequence attributes {first_production = @s1.$root::@s3.top::@s4.top::@s7::@s8.main, first_production_path = "top.main", has_first_production = true, node_id = 38 : i64, production_count = 2 : i64} {
            }
          }
        }
      }
    }
  }
}

