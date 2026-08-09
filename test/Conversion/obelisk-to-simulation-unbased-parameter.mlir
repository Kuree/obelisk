// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unbased_parameter", name = "unbased_parameter", node_id = 0 : i64, sym_name = "s0.unbased_parameter"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unbased_parameter", is_uninstantiated = false, name = "unbased_parameter", node_id = 3 : i64, referenced_path = "unbased_parameter", referenced_symbol = @s0.unbased_parameter, sym_name = "s3.unbased_parameter"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unbased_parameter", name = "unbased_parameter", node_id = 4 : i64, sym_name = "s4.unbased_parameter"} {
        obelisk.sv.symbol.parameter attributes {constant_value = "1'b1", hierarchical_name = "unbased_parameter.fill", name = "fill", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.fill"} {
          obelisk.sv.expression.unbased_unsized_integer_literal attributes {constant_value = "1'b1", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unbased_parameter.result", lifetime = 1 : i32, name = "result", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s7.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unbased_parameter", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "unbased_parameter.result", referenced_symbol = @s1.$root::@s3.unbased_parameter::@s4.unbased_parameter::@s7.result, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.conversion attributes {node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "unbased_parameter.fill", referenced_symbol = @s1.$root::@s3.unbased_parameter::@s4.unbased_parameter::@s5.fill, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: %[[WIDE:.*]] = obelisk_sim.logic.constant 1 : i4, 0 : i4 : !obelisk_sim.logic<4>
// CHECK: %[[ARRAY:.*]] = obelisk_sim.packed.unflatten %[[WIDE]]
// CHECK: obelisk_sim.ref.store %[[ARRAY]]
// CHECK-NOT: obelisk.sv.
