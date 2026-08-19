// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 10.9.1 and 10.9.2: a member or index key claims its own
// element and `default:` covers the rest. An index key is written in the
// array's declared range, so `[3:0]` puts index 2 at element ordinal 1.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "assignment_pattern_keys", name = "assignment_pattern_keys", node_id = 0 : i64, sym_name = "s0.assignment_pattern_keys"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "assignment_pattern_keys", is_uninstantiated = false, name = "assignment_pattern_keys", node_id = 3 : i64, referenced_path = "assignment_pattern_keys", referenced_symbol = @s0.assignment_pattern_keys, sym_name = "s3.assignment_pattern_keys"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "assignment_pattern_keys", name = "assignment_pattern_keys", node_id = 4 : i64, sym_name = "s4.assignment_pattern_keys", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assignment_pattern_keys.named", lifetime = 1 : i32, name = "named", node_id = 5 : i64, semantic_type = !obelisk.source_aggregate<"assignment_pattern_keys", false, false, false, false, false, false, 0, 96, 96, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "c", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s5.named"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assignment_pattern_keys.indexed", lifetime = 1 : i32, name = "indexed", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.indexed"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assignment_pattern_keys", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.source_aggregate<"assignment_pattern_keys", false, false, false, false, false, false, 0, 96, 96, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "c", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "assignment_pattern_keys.named", referenced_symbol = @s1.$root::@s3.assignment_pattern_keys::@s4.assignment_pattern_keys::@s5.named, semantic_type = !obelisk.source_aggregate<"assignment_pattern_keys", false, false, false, false, false, false, 0, 96, 96, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "c", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                  }
                  obelisk.sv.expression.structured_assignment_pattern attributes {has_default_setter = true, index_setter_count = 0 : i64, is_signed = false, member_setter_count = 1 : i64, member_setter_ordinals = array<i64: 1>, node_id = 13 : i64, semantic_type = !obelisk.source_aggregate<"assignment_pattern_keys", false, false, false, false, false, false, 0, 96, 96, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "c", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, type_setter_count = 0 : i64} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "20", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "40", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "assignment_pattern_keys.indexed", referenced_symbol = @s1.$root::@s3.assignment_pattern_keys::@s4.assignment_pattern_keys::@s6.indexed, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.structured_assignment_pattern attributes {has_default_setter = true, index_setter_count = 2 : i64, is_signed = false, member_setter_count = 0 : i64, member_setter_ordinals = array<i64>, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, type_setter_count = 0 : i64} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 22 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
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


// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-DAG: %[[FALSE:.*]] = obelisk_sim.logic.constant false, false
// CHECK-DAG: %[[TRUE:.*]] = obelisk_sim.logic.constant true, false
// CHECK-DAG: %[[TWENTY:.*]] = arith.constant 20 : i32
// CHECK-DAG: %[[FORTY:.*]] = arith.constant 40 : i32
// CHECK: obelisk_sim.aggregate.construct %[[FORTY]], %[[TWENTY]], %[[FORTY]] :
// CHECK: obelisk_sim.aggregate.construct %[[FALSE]], %[[TRUE]], %[[FALSE]], %[[TRUE]] :
// CHECK-SAME: -> !obelisk_sim.packed_array<3 : 0 x !obelisk_sim.logic<1>>
