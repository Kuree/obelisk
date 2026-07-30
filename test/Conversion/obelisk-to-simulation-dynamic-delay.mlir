// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_dynamic_delay", name = "simulation_dynamic_delay", node_id = 0 : i64, sym_name = "s0.simulation_dynamic_delay"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_dynamic_delay", is_uninstantiated = false, name = "simulation_dynamic_delay", node_id = 3 : i64, referenced_path = "simulation_dynamic_delay", referenced_symbol = @s0.simulation_dynamic_delay, sym_name = "s3.simulation_dynamic_delay"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_dynamic_delay", name = "simulation_dynamic_delay", node_id = 4 : i64, sym_name = "s4.simulation_dynamic_delay"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_dynamic_delay.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_dynamic_delay.amount", lifetime = 1 : i32, name = "amount", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.amount"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_dynamic_delay", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
              obelisk.sv.timing.delay attributes {node_id = 10 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "simulation_dynamic_delay.amount", referenced_symbol = @s1.$root::@s3.simulation_dynamic_delay::@s4.simulation_dynamic_delay::@s6.amount, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "simulation_dynamic_delay.value", referenced_symbol = @s1.$root::@s3.simulation_dynamic_delay::@s4.simulation_dynamic_delay::@s5.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "8'd1", node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
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

// CHECK-DAG: %[[MAX:.*]] = arith.constant 9223372036854775807 : i64
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK: %[[AMOUNT:.*]] = obelisk_sim.ref.load {{.*}} -> i32
// CHECK: %[[NONNEGATIVE:.*]] = arith.cmpi sge, %[[AMOUNT]], %[[ZERO]] : i32
// CHECK: %[[CLAMPED_LOW:.*]] = arith.select %[[NONNEGATIVE]], %[[AMOUNT]], %[[ZERO]] : i32
// CHECK: %[[WIDENED:.*]] = arith.extui %[[CLAMPED_LOW]] : i32 to i64
// CHECK: %[[IN_RANGE:.*]] = arith.cmpi ule, %[[WIDENED]], %[[MAX]] : i64
// CHECK: %[[CLAMPED_HIGH:.*]] = arith.select %[[IN_RANGE]], %[[WIDENED]], %[[MAX]] : i64
// CHECK: %[[DELAY:.*]] = obelisk_sim.time.scale %[[CLAMPED_HIGH]] by 1 signed = false : i64
// CHECK: obelisk_sim.suspend.delay %[[DELAY]]
// CHECK-NOT: obelisk.sv.
