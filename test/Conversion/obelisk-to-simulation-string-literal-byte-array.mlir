// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 5.9: a string literal can be assigned to an unpacked array of
// bytes, and if the size differs it is left justified.  "five" therefore fills
// `byte unpack1[0:4]` from its first element on, leaving the one element the
// literal does not reach at zero -- a layout the literal's packed
// representation cannot express, so the bytes are placed directly.

module {
  obelisk.sv.symbol.definition @s0.t attributes {
    definition_kind = 0 : i32, hierarchical_name = "t", name = "t",
    node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {
    }
    obelisk.sv.symbol.instance @s3.t attributes {
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t
    } {
      obelisk.sv.symbol.instance_body @s4.t attributes {
        hierarchical_name = "t", name = "t", node_id = 4 : i64,
        time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable @s5.unpack1 attributes {
          hierarchical_name = "t.unpack1", lifetime = 1 : i32,
          name = "unpack1", node_id = 5 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<0 : 4 x !obelisk.integral<8, true, false, 7 : 0, byte>>
        } {
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {
          hierarchical_name = "t", node_id = 6 : i64, procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 7 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false, node_id = 8 : i64,
              semantic_type = !obelisk.ranged_unpacked_array<0 : 4 x !obelisk.integral<8, true, false, 7 : 0, byte>>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 9 : i64,
                referenced_path = "t.unpack1",
                referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.unpack1,
                semantic_type = !obelisk.ranged_unpacked_array<0 : 4 x !obelisk.integral<8, true, false, 7 : 0, byte>>
              } {
              }
              obelisk.sv.expression.conversion attributes {
                is_signed = false, node_id = 10 : i64,
                semantic_type = !obelisk.ranged_unpacked_array<0 : 4 x !obelisk.integral<8, true, false, 7 : 0, byte>>
              } {
                obelisk.sv.expression.string_literal attributes {
                  constant_value = "five", is_signed = false,
                  node_id = 11 : i64,
                  semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
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
// CHECK-DAG: %[[F:.*]] = arith.constant 102 : i8
// CHECK-DAG: %[[I:.*]] = arith.constant 105 : i8
// CHECK-DAG: %[[V:.*]] = arith.constant 118 : i8
// CHECK-DAG: %[[E:.*]] = arith.constant 101 : i8
// CHECK-DAG: %[[PAD:.*]] = arith.constant 0 : i8
// CHECK: %[[ARRAY:.*]] = simulation.aggregate.construct %[[F]], %[[I]], %[[V]], %[[E]], %[[PAD]]
// CHECK-SAME: -> !simulation.unpacked_array<0 : 4 x i8>
// CHECK: simulation.ref.store %[[ARRAY]] to %arg1
