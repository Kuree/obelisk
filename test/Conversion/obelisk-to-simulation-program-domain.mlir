// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 2 : i32, hierarchical_name = "simulation_program_domain", name = "simulation_program_domain", node_id = 0 : i64, sym_name = "s0.simulation_program_domain"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_program_domain", is_uninstantiated = false, name = "simulation_program_domain", node_id = 3 : i64, referenced_path = "simulation_program_domain", referenced_symbol = @s0.simulation_program_domain, sym_name = "s3.simulation_program_domain"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_program_domain", name = "simulation_program_domain", node_id = 4 : i64, sym_name = "s4.simulation_program_domain"} {
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_program_domain", node_id = 5 : i64, procedure_kind = 0 : i32, sym_name = "s5", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 6 : i64} {
            obelisk.sv.timing.delay attributes {node_id = 7 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 9 : i64} {
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.func private @unit_{{.*}} attributes {
// CHECK-SAME: domain = 1 : i32
// CHECK-SAME: home_region = 10 : i32
// CHECK-NOT: obelisk.sv.
