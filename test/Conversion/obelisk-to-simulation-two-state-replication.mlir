// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.12.1: packed replication repeats the self-determined
// operand a constant number of times. This hand-authored semantic input guards
// the compact two-state lowering independently of the source frontend.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64, sym_name = "s0.m"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 2 : i64, referenced_path = "m", referenced_symbol = @s0.m, sym_name = "s2.m"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "m", name = "m", node_id = 3 : i64, sym_name = "s3.m", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "m.input_bit", lifetime = 1 : i32, name = "input_bit", node_id = 4 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s4.input_bit"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "m.result", lifetime = 1 : i32, name = "result", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s5.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "m", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "m.result", referenced_symbol = @s1.$root::@s2.m::@s3.m::@s5.result, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.replication attributes {is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "8", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.concatenation attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "m.input_bit", referenced_symbol = @s1.$root::@s2.m::@s3.m::@s4.input_bit, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
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

// CHECK: %[[BIT:.*]] = simulation.ref.load
// CHECK: %[[LOGIC:.*]] = simulation.logic.from_bits %[[BIT]]
// CHECK: %[[REPEATED:.*]] = simulation.logic.replicate %[[LOGIC]] times 8
// CHECK: %[[BITS:.*]] = simulation.logic.to_bits %[[REPEATED]]
// CHECK: %[[PACKED:.*]] = simulation.packed.unflatten %[[BITS]]
// CHECK: simulation.ref.store %[[PACKED]]
