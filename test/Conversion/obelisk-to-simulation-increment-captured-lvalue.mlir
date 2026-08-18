// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.2 defines increment and decrement as a read of the
// variable followed by a blocking assignment back to it, so they reach their
// destination the way a compound assignment (11.4.1) does: through a captured
// lvalue. A queue element (7.10) has no interior reference for its member to
// load and store through, and only the capture path can rebuild the element
// around the new value.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "queue_member_increment", name = "queue_member_increment", node_id = 0 : i64, sym_name = "s0.queue_member_increment"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "queue_member_increment", is_uninstantiated = false, name = "queue_member_increment", node_id = 3 : i64, referenced_path = "queue_member_increment", referenced_symbol = @s0.queue_member_increment, sym_name = "s3.queue_member_increment"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "queue_member_increment", name = "queue_member_increment", node_id = 4 : i64, sym_name = "s4.queue_member_increment", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.type.type_alias attributes {hierarchical_name = "queue_member_increment.s_t", name = "s_t", node_id = 5 : i64, semantic_type = !obelisk.source_aggregate<"queue_member_increment", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s5.s_t"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "queue_member_increment.q", lifetime = 1 : i32, name = "q", node_id = 6 : i64, semantic_type = !obelisk.queue<!obelisk.source_aggregate<"queue_member_increment", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, 0>, sym_name = "s6.q"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "queue_member_increment", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 9 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.member_access attributes {field_ordinal = 0 : i64, is_signed = true, member_name = "v", node_id = 10 : i64, packed_offset = 0 : i64, referenced_path = "queue_member_increment.v", referenced_symbol = @s1.$root::@s3.queue_member_increment::@s4.queue_member_increment::@s8::@s9.v, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.source_aggregate<"queue_member_increment", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "queue_member_increment.q", referenced_symbol = @s1.$root::@s3.queue_member_increment::@s4.queue_member_increment::@s6.q, semantic_type = !obelisk.queue<!obelisk.source_aggregate<"queue_member_increment", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, 0>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.type.unpacked_struct_type attributes {hierarchical_name = "queue_member_increment", node_id = 14 : i64, semantic_type = !obelisk.source_aggregate<"queue_member_increment", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s8"} {
          obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 0 : i64, hierarchical_name = "queue_member_increment.v", name = "v", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s9.v"} {
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK:      %[[ELEMENT:.*]] = obelisk_sim.container.read
// CHECK:      %[[OLD:.*]] = obelisk_sim.aggregate.extract %[[ELEMENT]][0]
// CHECK:      %[[NEW:.*]] = arith.addi %[[OLD]]
// CHECK:      %[[UPDATED:.*]] = obelisk_sim.aggregate.insert %[[NEW]] into %[[ELEMENT]][0]
// CHECK:      obelisk_sim.container.write {{.*}}, %[[UPDATED]]
