// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.6: `==?` yields x when the left operand holds an x or z
// against a non-wildcard bit of the right operand. A two-state left operand
// never can, so `bit [2:0] ==? logic [2:0]` has type `bit` even though the
// operands meet on the four-state plane. The comparison still belongs on that
// plane -- simulation.logic.compare requires a four-state result for the
// wildcard kinds -- and is narrowed to the expression type afterwards.
module {
  obelisk.sv.symbol.definition @s0.m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @s2.m attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 2 : i64, referenced_path = "m", referenced_symbol = @s0.m} {
      obelisk.sv.symbol.instance_body @s3.m attributes {hierarchical_name = "m", name = "m", node_id = 3 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s4.lhs attributes {hierarchical_name = "m.lhs", lifetime = 1 : i32, name = "lhs", node_id = 4 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
        }
        obelisk.sv.symbol.variable @s5.rhs attributes {hierarchical_name = "m.rhs", lifetime = 1 : i32, name = "rhs", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s6.result attributes {hierarchical_name = "m.result", lifetime = 1 : i32, name = "result", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "m", node_id = 7 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "m.result", referenced_symbol = @s1.$root::@s2.m::@s3.m::@s6.result, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              // operator_kind 17 is the wildcard equality operator.
              obelisk.sv.expression.binary_op attributes {node_id = 11 : i64, operator_kind = 17 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.conversion attributes {node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "m.lhs", referenced_symbol = @s1.$root::@s2.m::@s3.m::@s4.lhs, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "m.rhs", referenced_symbol = @s1.$root::@s2.m::@s3.m::@s5.rhs, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: %[[WILD:.*]] = simulation.logic.compare wild_eq %{{.*}}, %{{.*}} : (!simulation.logic<3>, !simulation.logic<3>) -> !simulation.logic<1>
// CHECK-NEXT: %[[BIT:.*]] = simulation.logic.to_bits %[[WILD]] : !simulation.logic<1> -> i1
// CHECK-NEXT: simulation.ref.store %[[BIT]]
