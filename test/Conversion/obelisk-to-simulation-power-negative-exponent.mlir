// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.4, Table 11-4: a negative exponent makes the result
// depend on the base alone -- 0 for any base outside {-1, 0, 1}, 1 for a base
// of 1, x for a base of 0, and the exponent's parity for a base of -1. The
// clause also makes the exponent self-determined, so only an exponent whose
// own type is signed can reach that half of the table.

module {
  obelisk.sv.symbol.definition @s0.power_negative_exponent attributes {definition_kind = 0 : i32, hierarchical_name = "power_negative_exponent", name = "power_negative_exponent", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.power_negative_exponent attributes {hierarchical_name = "power_negative_exponent", is_uninstantiated = false, name = "power_negative_exponent", node_id = 3 : i64, referenced_path = "power_negative_exponent", referenced_symbol = @s0.power_negative_exponent} {
      obelisk.sv.symbol.instance_body @s4.power_negative_exponent attributes {hierarchical_name = "power_negative_exponent", name = "power_negative_exponent", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.base attributes {hierarchical_name = "power_negative_exponent.base", lifetime = 1 : i32, name = "base", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s6.exponent attributes {hierarchical_name = "power_negative_exponent.exponent", lifetime = 1 : i32, name = "exponent", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s7.signed_result attributes {hierarchical_name = "power_negative_exponent.signed_result", lifetime = 1 : i32, name = "signed_result", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s8.unsigned_exponent attributes {hierarchical_name = "power_negative_exponent.unsigned_exponent", lifetime = 1 : i32, name = "unsigned_exponent", node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s9.unsigned_result attributes {hierarchical_name = "power_negative_exponent.unsigned_result", lifetime = 1 : i32, name = "unsigned_result", node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "power_negative_exponent", node_id = 10 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 13 : i64, referenced_path = "power_negative_exponent.signed_result", referenced_symbol = @s1.$root::@s3.power_negative_exponent::@s4.power_negative_exponent::@s7.signed_result, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 14 : i64, operator_kind = 27 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 15 : i64, referenced_path = "power_negative_exponent.base", referenced_symbol = @s1.$root::@s3.power_negative_exponent::@s4.power_negative_exponent::@s5.base, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "power_negative_exponent.exponent", referenced_symbol = @s1.$root::@s3.power_negative_exponent::@s4.power_negative_exponent::@s6.exponent, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "power_negative_exponent", node_id = 17 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 20 : i64, referenced_path = "power_negative_exponent.unsigned_result", referenced_symbol = @s1.$root::@s3.power_negative_exponent::@s4.power_negative_exponent::@s9.unsigned_result, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 21 : i64, operator_kind = 27 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 22 : i64, referenced_path = "power_negative_exponent.base", referenced_symbol = @s1.$root::@s3.power_negative_exponent::@s4.power_negative_exponent::@s5.base, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "power_negative_exponent.unsigned_exponent", referenced_symbol = @s1.$root::@s3.power_negative_exponent::@s4.power_negative_exponent::@s8.unsigned_exponent, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[BASE:.*]] = simulation.packed.flatten
// CHECK: %[[EXP:.*]] = simulation.packed.flatten
// CHECK: %[[SIGN:.*]] = simulation.logic.extract %[[EXP]] from 7
// CHECK: %[[MASKED:.*]] = simulation.logic.mux %[[SIGN]] ? %{{.*}} : %[[EXP]]
// CHECK: %[[MAGNITUDE:.*]] = simulation.logic.power %[[BASE]], %[[MASKED]]
// CHECK: %[[LOW:.*]] = simulation.logic.extract %[[EXP]] from 0
// CHECK: %[[MINUS_ONE:.*]] = simulation.logic.constant -1 : i8, 0 : i8
// CHECK: %[[ONE:.*]] = simulation.logic.constant 1 : i8, 0 : i8
// CHECK: %[[PARITY:.*]] = simulation.logic.mux %[[LOW]] ? %[[MINUS_ONE]] : %[[ONE]]
// CHECK: %[[IS_MINUS_ONE:.*]] = simulation.logic.compare eq %[[BASE]], %{{.*}}
// CHECK: %[[ZERO:.*]] = simulation.logic.constant 0 : i8, 0 : i8
// CHECK: %[[NEG1:.*]] = simulation.logic.mux %[[IS_MINUS_ONE]] ? %[[PARITY]] : %[[ZERO]]
// CHECK: %[[IS_ONE:.*]] = simulation.logic.compare eq %[[BASE]], %{{.*}}
// CHECK: %[[NEG2:.*]] = simulation.logic.mux %[[IS_ONE]] ? %{{.*}} : %[[NEG1]]
// CHECK: %[[IS_ZERO:.*]] = simulation.logic.compare eq %[[BASE]], %{{.*}}
// CHECK: %[[UNKNOWN:.*]] = simulation.logic.constant 0 : i8, -1 : i8
// CHECK: %[[NEG3:.*]] = simulation.logic.mux %[[IS_ZERO]] ? %[[UNKNOWN]] : %[[NEG2]]
// CHECK: simulation.logic.mux %[[SIGN]] ? %[[NEG3]] : %[[MAGNITUDE]]

// An unsigned exponent cannot be negative, so the table's lower half is not
// selected for at all and only the compact power operation and the
// x-propagating mux remain.
// CHECK-LABEL: simulation.func private @unit_1
// CHECK-NOT: simulation.logic.constant -1 : i8, 0 : i8
// CHECK: simulation.logic.power
// CHECK: simulation.ref.store
