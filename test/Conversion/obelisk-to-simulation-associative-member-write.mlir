// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 7.8: an associative array's element comes into existence when
// it is written, so the element holds no interior reference of its own.
// Writing one member of a structure element therefore rebuilds the element and
// writes it back, the same treatment a queue element gets.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "associative_member_write", name = "associative_member_write", node_id = 0 : i64, sym_name = "s0.associative_member_write"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "associative_member_write", is_uninstantiated = false, name = "associative_member_write", node_id = 3 : i64, referenced_path = "associative_member_write", referenced_symbol = @s0.associative_member_write, sym_name = "s3.associative_member_write"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "associative_member_write", name = "associative_member_write", node_id = 4 : i64, sym_name = "s4.associative_member_write", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.type.type_alias attributes {hierarchical_name = "associative_member_write.result_t", name = "result_t", node_id = 5 : i64, semantic_type = !obelisk.source_aggregate<"associative_member_write", false, false, false, false, false, false, 0, 64, 64, 0, [{name = "fails", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "passs", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s5.result_t"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "associative_member_write.results", lifetime = 1 : i32, name = "results", node_id = 6 : i64, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.source_aggregate<"associative_member_write", false, false, false, false, false, false, 0, 64, 64, 0, [{name = "fails", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "passs", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, false>, sym_name = "s6.results"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "associative_member_write", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.member_access attributes {field_ordinal = 1 : i64, is_signed = true, member_name = "passs", node_id = 11 : i64, packed_offset = 32 : i64, referenced_path = "associative_member_write.passs", referenced_symbol = @s1.$root::@s3.associative_member_write::@s4.associative_member_write::@s8::@s10.passs, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.source_aggregate<"associative_member_write", false, false, false, false, false, false, 0, 64, 64, 0, [{name = "fails", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "passs", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "associative_member_write.results", referenced_symbol = @s1.$root::@s3.associative_member_write::@s4.associative_member_write::@s6.results, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.source_aggregate<"associative_member_write", false, false, false, false, false, false, 0, 64, 64, 0, [{name = "fails", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "passs", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, false>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.type.unpacked_struct_type attributes {hierarchical_name = "associative_member_write", node_id = 16 : i64, semantic_type = !obelisk.source_aggregate<"associative_member_write", false, false, false, false, false, false, 0, 64, 64, 0, [{name = "fails", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "passs", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s8"} {
          obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 0 : i64, hierarchical_name = "associative_member_write.fails", name = "fails", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s9.fails"} {
          }
          obelisk.sv.symbol.field attributes {bit_offset = 32 : i64, field_index = 1 : i64, hierarchical_name = "associative_member_write.passs", name = "passs", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s10.passs"} {
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// 7.8.6: reading a key that is not there yet yields the element type's default
// value, so the element the member is written into comes from either arm of
// that choice.
// CHECK:      obelisk_sim.aggregate.default
// CHECK:      obelisk_sim.assoc.read
// CHECK:      %[[UPDATED:.*]] = obelisk_sim.aggregate.insert {{.*}}[1]
// CHECK:      obelisk_sim.assoc.write {{.*}}, %[[UPDATED]]
