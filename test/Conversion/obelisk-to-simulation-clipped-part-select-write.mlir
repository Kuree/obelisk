// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.5.1: a part-select that is partially out of range affects
// only the bits that are in range when written. Which bits of the assigned
// value survive depends on which end is clipped: `x[0:-1]` drops the value's
// bit 0 below the vector and writes its bit 1 to x[0], while `x[4:3]` drops
// bit 1 above the vector and writes bit 0 to x[3].

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "clipped_part_select", name = "clipped_part_select", node_id = 0 : i64, sym_name = "s0.clipped_part_select"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "clipped_part_select", is_uninstantiated = false, name = "clipped_part_select", node_id = 3 : i64, referenced_path = "clipped_part_select", referenced_symbol = @s0.clipped_part_select, sym_name = "s3.clipped_part_select"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "clipped_part_select", name = "clipped_part_select", node_id = 4 : i64, sym_name = "s4.clipped_part_select", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "clipped_part_select.x", lifetime = 1 : i32, name = "x", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "clipped_part_select.r", lifetime = 1 : i32, name = "r", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.r"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "clipped_part_select", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<0 : -1 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 12 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<0 : -1 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "clipped_part_select.x", referenced_symbol = @s1.$root::@s3.clipped_part_select::@s4.clipped_part_select::@s5.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.unary_op attributes {folded_constant = "-1", is_signed = true, node_id = 15 : i64, operator_kind = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "clipped_part_select.r", referenced_symbol = @s1.$root::@s3.clipped_part_select::@s4.clipped_part_select::@s6.r, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<4 : 3 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 20 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<4 : 3 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "clipped_part_select.x", referenced_symbol = @s1.$root::@s3.clipped_part_select::@s4.clipped_part_select::@s5.x, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", is_declared_unsized = true, is_signed = true, node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "clipped_part_select.r", referenced_symbol = @s1.$root::@s3.clipped_part_select::@s4.clipped_part_select::@s6.r, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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

// CHECK: %[[LOW_VALUE:.*]] = obelisk_sim.logic.extract %{{.*}} from 1 : !obelisk_sim.logic<2> -> !obelisk_sim.logic<1>
// CHECK: %[[LOW_BIT:.*]] = obelisk_sim.ref.extract %[[X]] from 0
// CHECK: obelisk_sim.ref.store %[[LOW_VALUE]] to %[[LOW_BIT]]

// CHECK: %[[HIGH_VALUE:.*]] = obelisk_sim.logic.extract %{{.*}} from 0 : !obelisk_sim.logic<2> -> !obelisk_sim.logic<1>
// CHECK: %[[HIGH_BIT:.*]] = obelisk_sim.ref.extract %[[X]] from 3
// CHECK: obelisk_sim.ref.store %[[HIGH_VALUE]] to %[[HIGH_BIT]]
