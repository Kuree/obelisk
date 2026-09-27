// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.12.2 and 12.7.2: integral-to-shortreal conversion
// treats individual X/Z bits as zero, and a real repeat count uses normal
// rounded real-to-integral conversion. Keep both conversions explicit in
// Simulation IR so native and bytecode tiers share the same semantics.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "real_repeat_shortreal", name = "real_repeat_shortreal", node_id = 0 : i64, sym_name = "s0.real_repeat_shortreal"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "real_repeat_shortreal", is_uninstantiated = false, name = "real_repeat_shortreal", node_id = 3 : i64, referenced_path = "real_repeat_shortreal", referenced_symbol = @s0.real_repeat_shortreal, sym_name = "s3.real_repeat_shortreal"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "real_repeat_shortreal", name = "real_repeat_shortreal", node_id = 4 : i64, sym_name = "s4.real_repeat_shortreal"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "real_repeat_shortreal.index", lifetime = 1 : i32, name = "index", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.index"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "real_repeat_shortreal.value", lifetime = 1 : i32, name = "value", node_id = 6 : i64, semantic_type = !obelisk.shortreal, sym_name = "s6.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "real_repeat_shortreal", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.shortreal} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "real_repeat_shortreal.value", referenced_symbol = @s1.$root::@s3.real_repeat_shortreal::@s4.real_repeat_shortreal::@s6.value, semantic_type = !obelisk.shortreal} {
                  }
                  obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.shortreal} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "real_repeat_shortreal.index", referenced_symbol = @s1.$root::@s3.real_repeat_shortreal::@s4.real_repeat_shortreal::@s5.index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.repeat_loop attributes {node_id = 15 : i64} {
                obelisk.sv.expression.real_literal attributes {constant_value = "10.4", is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.real} {
                }
                obelisk.sv.statement.empty attributes {node_id = 17 : i64} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: %[[INDEX:.*]] = simulation.ref.load
// CHECK-DAG: %[[FLAT:.*]] = simulation.packed.flatten %[[INDEX]]
// CHECK-DAG: %[[BITS:.*]] = simulation.logic.to_bits %[[FLAT]] : !simulation.logic<32> -> i32
// CHECK-DAG: simulation.real.from_integer %[[BITS]] signed = false : i32 -> f32
// CHECK-DAG: %[[COUNT:.*]] = arith.constant 1.040000e+01 : f64
// CHECK-DAG: %[[ROUNDED:.*]] = simulation.real.to_integer %[[COUNT]] signed = false : i64
// CHECK-DAG: cf.br ^{{.*}}(%[[ROUNDED]] : i64)
// CHECK-NOT: obelisk.sv.
