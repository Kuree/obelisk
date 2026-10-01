// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Runtime behavior is checked in ../Runtime/obelisk-to-simulation-program-final-region.test.

// Program procedures normally execute in the Reactive region, but `final`
// procedures use the runtime final-phase ABI and therefore retain an Active
// process home. Their compute fragment is still planned in Postponed for
// end-of-simulation execution.
// CHECK: #schedule.fragment<{{.*}}function = @unit_1{{.*}}region = postponed
// CHECK-LABEL: simulation.func private @unit_0
// CHECK-SAME: domain = 1 : i32
// CHECK-SAME: entry_kind = 1 : i32
// CHECK-SAME: home_region = 10 : i32
// CHECK-LABEL: simulation.func private @unit_1
// CHECK-SAME: domain = 1 : i32
// CHECK-SAME: entry_kind = 2 : i32
// CHECK-SAME: home_region = 2 : i32

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.program_final attributes {
    definition_kind = 2 : i32, hierarchical_name = "program_final",
    name = "program_final", node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {
    }
    obelisk.sv.symbol.instance @s3.program_final attributes {
      hierarchical_name = "program_final", is_uninstantiated = false,
      name = "program_final", node_id = 3 : i64,
      referenced_path = "program_final",
      referenced_symbol = @s0.program_final
    } {
      obelisk.sv.symbol.instance_body @s4.program_final attributes {
        hierarchical_name = "program_final", name = "program_final",
        node_id = 4 : i64,
        time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.procedural_block @s5 attributes {
          hierarchical_name = "program_final", node_id = 5 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.empty attributes {node_id = 6 : i64} {
          }
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {
          hierarchical_name = "program_final", node_id = 7 : i64,
          procedure_kind = 1 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.empty attributes {node_id = 8 : i64} {
          }
        }
      }
    }
  }
}
