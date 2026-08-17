// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.5.1: a part-select that is partially out of range returns
// x for the bits that are out of range when read, and affects only the bits
// that are in range when written. A select wider than the value it addresses
// is always partially out of range, so both directions place the value in a
// window padded on either side with out-of-range bits: `x[4:-1]` reads one
// padding bit at each end, and `x[7:2]` writes only bits 3 and 2.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "wide_part_select", name = "wide_part_select", node_id = 0 : i64, sym_name = "s0.wide_part_select"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "wide_part_select", is_uninstantiated = false, name = "wide_part_select", node_id = 3 : i64, referenced_path = "wide_part_select", referenced_symbol = @s0.wide_part_select, sym_name = "s3.wide_part_select"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "wide_part_select", name = "wide_part_select", node_id = 4 : i64, sym_name = "s4.wide_part_select", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wide_part_select.x", lifetime = 1 : i32, name = "x", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wide_part_select.r", lifetime = 1 : i32, name = "r", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<5 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.r"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "wide_part_select", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<5 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "wide_part_select.r", referenced_symbol = @s1.$root::@s3.wide_part_select::@s4.wide_part_select::@s6.r, semantic_type = !obelisk.ranged_packed_array<5 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 13 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<4 : -1 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "wide_part_select.x", referenced_symbol = @s1.$root::@s3.wide_part_select::@s4.wide_part_select::@s5.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.unary_op attributes {folded_constant = "-1", is_signed = true, node_id = 16 : i64, operator_kind = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 2 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 20 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 2 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "wide_part_select.x", referenced_symbol = @s1.$root::@s3.wide_part_select::@s4.wide_part_select::@s5.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "wide_part_select.r", referenced_symbol = @s1.$root::@s3.wide_part_select::@s4.wide_part_select::@s6.r, semantic_type = !obelisk.ranged_packed_array<5 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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


// CHECK: obelisk_sim.func private @unit_0(
// CHECK-SAME: %[[X:[^:]*]]: !obelisk_sim.ref<!obelisk_sim.packed_array<3 : 0 x !obelisk_sim.logic<1>>>
// CHECK-SAME: %[[R:[^:]*]]: !obelisk_sim.ref<!obelisk_sim.packed_array<5 : 0 x !obelisk_sim.logic<1>>>

// Six bits of padding on either side of the four-bit value, all unknown.
// CHECK: %[[PAD:.*]] = obelisk_sim.logic.constant 0 : i16, -1 : i16 : !obelisk_sim.logic<16>

// The read window starts one bit below the value, so it takes an x from each
// end of the padding.
// CHECK: %[[READ_BASE:.*]] = obelisk_sim.logic.insert %{{.*}} into %[[PAD]] at 6
// CHECK: obelisk_sim.logic.extract %[[READ_BASE]] from 5 : !obelisk_sim.logic<16> -> !obelisk_sim.logic<6>

// The write puts all six bits into the same padded window and then keeps only
// the four that were in range, dropping the rest with the padding.
// CHECK: %[[WRITE_BASE:.*]] = obelisk_sim.logic.insert %{{.*}} into %[[PAD]] at 6
// CHECK: %[[UPDATED:.*]] = obelisk_sim.logic.insert %{{.*}} into %[[WRITE_BASE]] at 8
// CHECK: obelisk_sim.logic.extract %[[UPDATED]] from 6 : !obelisk_sim.logic<16> -> !obelisk_sim.logic<4>
// CHECK: obelisk_sim.ref.store %{{.*}} to %[[X]]
