// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.12.2: "Implicit conversion shall also take place when an
// expression is assigned to a real. Individual bits that are x or z in the net
// or the variable shall be treated as zero upon conversion."
//
// The rule is per bit, which is exactly what logic.to_bits performs. Guarding
// the conversion on the whole value being known and substituting zero
// otherwise discards the known bits along with the unknown ones, so
// 8'b1x1z0001 converts to 0.0 instead of 161.0.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "real_conversion_top", name = "real_conversion_top", node_id = 0 : i64, sym_name = "s0.real_conversion_top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "real_conversion_top", is_uninstantiated = false, name = "real_conversion_top", node_id = 3 : i64, referenced_path = "real_conversion_top", referenced_symbol = @s0.real_conversion_top, sym_name = "s3.real_conversion_top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "real_conversion_top", name = "real_conversion_top", node_id = 4 : i64, sym_name = "s4.real_conversion_top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "real_conversion_top.mixed", lifetime = 1 : i32, name = "mixed", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.mixed"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "real_conversion_top.converted", lifetime = 1 : i32, name = "converted", node_id = 6 : i64, semantic_type = !obelisk.real, sym_name = "s6.converted"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "real_conversion_top", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "real_conversion_top.mixed", referenced_symbol = @s1.$root::@s3.real_conversion_top::@s4.real_conversion_top::@s5.mixed, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "8'b1x1z0001", is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.real} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "real_conversion_top.converted", referenced_symbol = @s1.$root::@s3.real_conversion_top::@s4.real_conversion_top::@s6.converted, semantic_type = !obelisk.real} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.real} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "real_conversion_top.mixed", referenced_symbol = @s1.$root::@s3.real_conversion_top::@s4.real_conversion_top::@s5.mixed, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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
// CHECK:      %[[BITS:.*]] = obelisk_sim.logic.to_bits %{{.*}} : !obelisk_sim.logic<8> -> i8
// CHECK-NEXT: obelisk_sim.real.from_integer %[[BITS]] signed = false : i8 -> f64

// No all-or-nothing unknown guard: the round trip through logic.from_bits, the
// case-equality test against it, and the zero select are what dropped the
// known bits.
// CHECK-NOT: obelisk_sim.logic.from_bits
// CHECK-NOT: arith.select
