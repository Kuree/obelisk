// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

module {
  talos.sv.symbol.definition @s0.wide_bitwise_not attributes {definition_kind = 0 : i32, hierarchical_name = "wide_bitwise_not", name = "wide_bitwise_not", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.wide_bitwise_not attributes {hierarchical_name = "wide_bitwise_not", is_uninstantiated = false, name = "wide_bitwise_not", node_id = 3 : i64, referenced_path = "wide_bitwise_not", referenced_symbol = @s0.wide_bitwise_not} {
      talos.sv.symbol.instance_body @s4.wide_bitwise_not attributes {hierarchical_name = "wide_bitwise_not", name = "wide_bitwise_not", node_id = 4 : i64} {
        talos.sv.symbol.variable @s5.result attributes {hierarchical_name = "wide_bitwise_not.result", lifetime = 1 : i32, name = "result", node_id = 5 : i64, semantic_type = !talos.integral<90, false, false, 89 : 0, bit>} {
        }
        talos.sv.symbol.procedural_block @s6 attributes {hierarchical_name = "wide_bitwise_not", node_id = 6 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 8 : i64, semantic_type = !talos.integral<90, false, false, 89 : 0, bit>} {
              talos.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "wide_bitwise_not.result", referenced_symbol = @s1.$root::@s3.wide_bitwise_not::@s4.wide_bitwise_not::@s5.result, semantic_type = !talos.integral<90, false, false, 89 : 0, bit>} {
              }
              talos.sv.expression.unary_op attributes {node_id = 10 : i64, operator_kind = 2 : i32, semantic_type = !talos.integral<90, false, false, 89 : 0, bit>} {
                talos.sv.expression.integer_literal attributes {constant_value = "90'h0", node_id = 11 : i64, semantic_type = !talos.integral<90, false, false, 89 : 0, bit>} {
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
// CHECK: simulation.ref.store %[[ONES]] to
// CHECK-NOT: talos.sv.
