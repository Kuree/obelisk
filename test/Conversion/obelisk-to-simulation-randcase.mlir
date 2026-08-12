// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// randcase (IEEE 1800-2017 18.16) evaluates every weight once, draws from the
// process random number generator over their sum, and selects the item whose
// cumulative bound first exceeds the draw. The weights are variables so the
// selection arithmetic survives constant folding.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "randcase_weights", name = "randcase_weights", node_id = 0 : i64, sym_name = "s0.randcase_weights"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "randcase_weights", is_uninstantiated = false, name = "randcase_weights", node_id = 3 : i64, referenced_path = "randcase_weights", referenced_symbol = @s0.randcase_weights, sym_name = "s3.randcase_weights"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "randcase_weights", name = "randcase_weights", node_id = 4 : i64, sym_name = "s4.randcase_weights", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "randcase_weights.wa", lifetime = 1 : i32, name = "wa", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s5.wa"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "randcase_weights.wb", lifetime = 1 : i32, name = "wb", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.wb"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "randcase_weights.chosen", lifetime = 1 : i32, name = "chosen", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s7.chosen"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "randcase_weights", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.rand_case attributes {item_count = 2 : i64, node_id = 10 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "randcase_weights.wa", referenced_symbol = @s1.$root::@s3.randcase_weights::@s4.randcase_weights::@s5.wa, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
            obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 12 : i64, referenced_path = "randcase_weights.wb", referenced_symbol = @s1.$root::@s3.randcase_weights::@s4.randcase_weights::@s6.wb, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "randcase_weights.chosen", referenced_symbol = @s1.$root::@s3.randcase_weights::@s4.randcase_weights::@s7.chosen, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2'b1", is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "randcase_weights.chosen", referenced_symbol = @s1.$root::@s3.randcase_weights::@s4.randcase_weights::@s7.chosen, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2'b10", is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// Each signed weight is clamped at zero, so a negative one contributes nothing
// instead of reappearing as a dominant unsigned magnitude. The running total
// after each item is that item's upper bound, so the first item's bound is its
// own clamped weight.
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i64
// CHECK: arith.cmpi slt, %{{.*}}, %[[ZERO]]
// CHECK: %[[BOUND0:.*]] = arith.select
// CHECK: arith.cmpi slt, %{{.*}}, %[[ZERO]]
// CHECK: %[[SECOND:.*]] = arith.select
// CHECK: %[[TOTAL:.*]] = arith.addi %[[BOUND0]], %[[SECOND]]

// An all-zero weight list selects no branch at all.
// CHECK: %[[ANY:.*]] = arith.cmpi ne, %[[TOTAL]], %[[ZERO]]
// CHECK: cf.cond_br %[[ANY]], ^[[SELECT:[^ ,]*]], ^[[MERGE:[^ ,]*]]

// CHECK: ^[[SELECT]]:
// CHECK: %[[DRAW:.*]] = obelisk_sim.random.bounded %{{.*}}, %[[TOTAL]]
// CHECK: %[[SELECTED:.*]] = arith.cmpi ult, %[[DRAW]], %[[BOUND0]]
// CHECK: cf.cond_br %[[SELECTED]], ^[[ITEM0:[^ ,]*]], ^[[ITEM1:[^ ,]*]]

// The draw is always below the total, so the last item needs no test.
// CHECK: ^[[ITEM1]]:
// CHECK-NOT: arith.cmpi ult
// CHECK: cf.br ^[[MERGE]]
