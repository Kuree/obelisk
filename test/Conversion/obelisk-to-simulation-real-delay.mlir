// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_real_delay", name = "simulation_real_delay", node_id = 0 : i64, sym_name = "s0.simulation_real_delay"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_real_delay", is_uninstantiated = false, name = "simulation_real_delay", node_id = 3 : i64, referenced_path = "simulation_real_delay", referenced_symbol = @s0.simulation_real_delay, sym_name = "s3.simulation_real_delay"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_real_delay", name = "simulation_real_delay", node_id = 4 : i64, sym_name = "s4.simulation_real_delay"} {
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_real_delay", node_id = 5 : i64, procedure_kind = 0 : i32, sym_name = "s5", time_precision_fs = 100000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 6 : i64} {
            obelisk.sv.statement.list attributes {node_id = 7 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 8 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 9 : i64} {
                  obelisk.sv.expression.real_literal attributes {constant_value = "0.14000000000000001", node_id = 10 : i64, semantic_type = !obelisk.real} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 11 : i64} {
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 12 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 13 : i64} {
                  obelisk.sv.expression.real_literal attributes {constant_value = "0.14999999999999999", node_id = 14 : i64, semantic_type = !obelisk.real} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 15 : i64} {
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 16 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 17 : i64} {
                  obelisk.sv.expression.time_literal attributes {constant_value = "1", node_id = 18 : i64, semantic_type = !obelisk.realtime, time_scale = "1ns / 100ps"} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 19 : i64} {
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 20 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 21 : i64} {
                  obelisk.sv.expression.unary_op attributes {node_id = 22 : i64, operator_kind = 1 : i32, semantic_type = !obelisk.real} {
                    obelisk.sv.expression.real_literal attributes {constant_value = "0.20000000000000001", node_id = 23 : i64, semantic_type = !obelisk.real} {
                    }
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 24 : i64} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// Real delays are rounded to the lexical 100 ps precision before they become
// design ticks: 0.14 ns -> 1 tick, 0.15 ns -> 2 ticks, 1 ns -> 10 ticks,
// and a negative delay -> 0 ticks.
// CHECK: obelisk_sim.design @design attributes {{.*}}time_precision_fs = 100000
// CHECK-DAG: obelisk_sim.time.constant 1{{$}}
// CHECK-DAG: obelisk_sim.time.constant 2{{$}}
// CHECK-DAG: obelisk_sim.time.constant 10{{$}}
// CHECK-DAG: obelisk_sim.time.constant 0{{$}}
// CHECK-COUNT-4: obelisk_sim.suspend.delay
// CHECK-NOT: obelisk.sv.
