// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 10.10: an unpacked array concatenation composes the target
// from items that are each either one element or an unpacked array of them,
// arranged left to right. Both targets here are fixed-size, so every item's
// element count is known and the result is built directly.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unpacked_concatenation", name = "unpacked_concatenation", node_id = 0 : i64, sym_name = "s0.unpacked_concatenation"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unpacked_concatenation", is_uninstantiated = false, name = "unpacked_concatenation", node_id = 3 : i64, referenced_path = "unpacked_concatenation", referenced_symbol = @s0.unpacked_concatenation, sym_name = "s3.unpacked_concatenation"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unpacked_concatenation", name = "unpacked_concatenation", node_id = 4 : i64, sym_name = "s4.unpacked_concatenation", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.type.type_alias attributes {hierarchical_name = "unpacked_concatenation.ai3_t", name = "ai3_t", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 3 x !obelisk.integral<32, true, false, 31 : 0, int>>, sym_name = "s5.ai3_t"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unpacked_concatenation.a3", lifetime = 1 : i32, name = "a3", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 3 x !obelisk.integral<32, true, false, 31 : 0, int>>, sym_name = "s6.a3"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unpacked_concatenation.a9", lifetime = 1 : i32, name = "a9", node_id = 7 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 9 x !obelisk.integral<32, true, false, 31 : 0, int>>, sym_name = "s7.a9"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unpacked_concatenation.s0", lifetime = 1 : i32, name = "s0", node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s8.s0"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unpacked_concatenation.descending", lifetime = 1 : i32, name = "descending", node_id = 9 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s9.descending"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unpacked_concatenation.s4", lifetime = 1 : i32, name = "s4", node_id = 10 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 4 x !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s10.s4"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unpacked_concatenation", node_id = 11 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 12 : i64} {
            obelisk.sv.statement.list attributes {node_id = 13 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 9 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "unpacked_concatenation.a9", referenced_symbol = @s1.$root::@s3.unpacked_concatenation::@s4.unpacked_concatenation::@s7.a9, semantic_type = !obelisk.ranged_unpacked_array<1 : 9 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                  }
                  obelisk.sv.expression.concatenation attributes {is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 9 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "unpacked_concatenation.a3", referenced_symbol = @s1.$root::@s3.unpacked_concatenation::@s4.unpacked_concatenation::@s6.a3, semantic_type = !obelisk.ranged_unpacked_array<1 : 3 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "unpacked_concatenation.a3", referenced_symbol = @s1.$root::@s3.unpacked_concatenation::@s4.unpacked_concatenation::@s6.a3, semantic_type = !obelisk.ranged_unpacked_array<1 : 3 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "6", is_declared_unsized = true, is_signed = true, node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 4 x !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "unpacked_concatenation.s4", referenced_symbol = @s1.$root::@s3.unpacked_concatenation::@s4.unpacked_concatenation::@s10.s4, semantic_type = !obelisk.ranged_unpacked_array<1 : 4 x !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  }
                  obelisk.sv.expression.concatenation attributes {is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 4 x !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "unpacked_concatenation.s0", referenced_symbol = @s1.$root::@s3.unpacked_concatenation::@s4.unpacked_concatenation::@s8.s0, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "unpacked_concatenation.descending", referenced_symbol = @s1.$root::@s3.unpacked_concatenation::@s4.unpacked_concatenation::@s9.descending, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}


// `{a3, 4, 5, a3, 6}` spreads the three elements of `a3` twice among the four
// single-element items, filling the nine-element target in item order.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-DAG: %[[C4:.*]] = arith.constant 4 : i32
// CHECK-DAG: %[[C5:.*]] = arith.constant 5 : i32
// CHECK-DAG: %[[C6:.*]] = arith.constant 6 : i32
// CHECK: %[[A0:.*]] = obelisk_sim.ref.load
// CHECK: %[[A1:.*]] = obelisk_sim.ref.load
// CHECK: %[[A2:.*]] = obelisk_sim.ref.load
// CHECK: obelisk_sim.aggregate.construct %[[A0]], %[[A1]], %[[A2]], %[[C4]], %[[C5]], %[[A0]], %[[A1]], %[[A2]], %[[C6]] :
// CHECK-SAME: -> !obelisk_sim.unpacked_array<1 : 9 x i32>

// A descending item keeps its own left-to-right order, which is the order the
// aggregate already indexes, so no reversal is introduced.
// CHECK: %[[S0:.*]] = obelisk_sim.ref.load %arg3
// CHECK: obelisk_sim.ref.subelement %arg4{{\[\[}}0]]
// CHECK: %[[D0:.*]] = obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.subelement %arg4{{\[\[}}1]]
// CHECK: %[[D1:.*]] = obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.subelement %arg4{{\[\[}}2]]
// CHECK: %[[D2:.*]] = obelisk_sim.ref.load
// CHECK: obelisk_sim.aggregate.construct %[[S0]], %[[D0]], %[[D1]], %[[D2]] :
