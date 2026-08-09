// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "binary_signedness", name = "binary_signedness", node_id = 0 : i64, sym_name = "s0.binary_signedness"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "binary_signedness", is_uninstantiated = false, name = "binary_signedness", node_id = 3 : i64, referenced_path = "binary_signedness", referenced_symbol = @s0.binary_signedness, sym_name = "s3.binary_signedness"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "binary_signedness", name = "binary_signedness", node_id = 4 : i64, sym_name = "s4.binary_signedness"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "binary_signedness.lhs", lifetime = 1 : i32, name = "lhs", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s5.lhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "binary_signedness.rhs", lifetime = 1 : i32, name = "rhs", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.rhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "binary_signedness.equal", lifetime = 1 : i32, name = "equal", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.equal"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "binary_signedness", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "binary_signedness.equal", referenced_symbol = @s1.$root::@s3.binary_signedness::@s4.binary_signedness::@s7.equal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.binary_op attributes {node_id = 12 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.conversion attributes {node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "binary_signedness.lhs", referenced_symbol = @s1.$root::@s3.binary_signedness::@s4.binary_signedness::@s5.lhs, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                  }
                }
                obelisk.sv.expression.conversion attributes {node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "binary_signedness.rhs", referenced_symbol = @s1.$root::@s3.binary_signedness::@s4.binary_signedness::@s6.rhs, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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

// CHECK: %[[LHS:.*]] = obelisk_sim.ref.load
// CHECK: %[[FLAT:.*]] = obelisk_sim.packed.flatten %[[LHS]]
// CHECK: %[[EXTENDED:.*]] = obelisk_sim.logic.resize %[[FLAT]] signed = false : !obelisk_sim.logic<2> -> !obelisk_sim.logic<3>
// CHECK: obelisk_sim.logic.compare eq %[[EXTENDED]],
// CHECK-NOT: obelisk.sv.
