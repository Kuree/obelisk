// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "wide_bitwise_not", name = "wide_bitwise_not", node_id = 0 : i64, sym_name = "s0.wide_bitwise_not"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "wide_bitwise_not", is_uninstantiated = false, name = "wide_bitwise_not", node_id = 3 : i64, referenced_path = "wide_bitwise_not", referenced_symbol = @s0.wide_bitwise_not, sym_name = "s3.wide_bitwise_not"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "wide_bitwise_not", name = "wide_bitwise_not", node_id = 4 : i64, sym_name = "s4.wide_bitwise_not"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wide_bitwise_not.result", lifetime = 1 : i32, name = "result", node_id = 5 : i64, semantic_type = !obelisk.integral<90, false, false, 89 : 0, bit>, sym_name = "s5.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "wide_bitwise_not", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 8 : i64, semantic_type = !obelisk.integral<90, false, false, 89 : 0, bit>} {
              obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "wide_bitwise_not.result", referenced_symbol = @s1.$root::@s3.wide_bitwise_not::@s4.wide_bitwise_not::@s5.result, semantic_type = !obelisk.integral<90, false, false, 89 : 0, bit>} {
              }
              obelisk.sv.expression.unary_op attributes {node_id = 10 : i64, operator_kind = 2 : i32, semantic_type = !obelisk.integral<90, false, false, 89 : 0, bit>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "90'h0", node_id = 11 : i64, semantic_type = !obelisk.integral<90, false, false, 89 : 0, bit>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: %[[ONES:.*]] = arith.constant -1 : i90
// CHECK: obelisk_sim.ref.store %[[ONES]] to
// CHECK-NOT: obelisk.sv.
