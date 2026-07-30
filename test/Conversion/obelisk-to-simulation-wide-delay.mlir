// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_wide_delay", name = "simulation_wide_delay", node_id = 0 : i64, sym_name = "s0.simulation_wide_delay"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_wide_delay", is_uninstantiated = false, name = "simulation_wide_delay", node_id = 3 : i64, referenced_path = "simulation_wide_delay", referenced_symbol = @s0.simulation_wide_delay, sym_name = "s3.simulation_wide_delay"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_wide_delay", name = "simulation_wide_delay", node_id = 4 : i64, sym_name = "s4.simulation_wide_delay"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_wide_delay.amount", lifetime = 1 : i32, name = "amount", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.amount"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_wide_delay", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 7 : i64} {
            obelisk.sv.timing.delay attributes {node_id = 8 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "simulation_wide_delay.amount", referenced_symbol = @s1.$root::@s3.simulation_wide_delay::@s4.simulation_wide_delay::@s5.amount, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "simulation_wide_delay.amount", referenced_symbol = @s1.$root::@s3.simulation_wide_delay::@s4.simulation_wide_delay::@s5.amount, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.conversion attributes {node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.conversion attributes {node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: dynamic delay wider than 64 bits is not executable
