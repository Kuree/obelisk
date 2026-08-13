// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "dpi_values", name = "dpi_values", node_id = 0 : i64, sym_name = "s0.dpi_values"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "dpi_values", is_uninstantiated = false, name = "dpi_values", node_id = 3 : i64, referenced_path = "dpi_values", referenced_symbol = @s0.dpi_values, sym_name = "s3.dpi_values"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "dpi_values", name = "dpi_values", node_id = 4 : i64, sym_name = "s4.dpi_values"} {
        obelisk.sv.symbol.subroutine attributes {dpi_c_identifier = "consume", hierarchical_name = "dpi_values.consume", is_dpi_import, name = "consume", node_id = 5 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s5.consume", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "dpi_values.consume.value", name = "value", node_id = 7 : i64, semantic_type = !obelisk.string, sym_name = "s6.value"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {dpi_c_identifier = "bounce", hierarchical_name = "dpi_values.bounce", is_dpi_import, name = "bounce", node_id = 8 : i64, semantic_type = !obelisk.subroutine<(!obelisk.chandle) -> !obelisk.chandle, false>, subroutine_kind = 0 : i32, sym_name = "s8.bounce", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 9 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "dpi_values.bounce.value", name = "value", node_id = 10 : i64, semantic_type = !obelisk.chandle, sym_name = "s10.value"} {
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.code_unit.decl {{.*}} hierarchy "dpi_values.consume"
// CHECK-SAME: #obelisk_sim.dpi_abi<kind = string, direction = input, width = 64, fourState = false, isSigned = false>
// CHECK-SAME: obelisk_sim.dpi_c_identifier = "consume"
// CHECK: obelisk_sim.code_unit.decl {{.*}} hierarchy "dpi_values.bounce"
// CHECK-SAME: #obelisk_sim.dpi_abi<kind = chandle, direction = input, width = 64, fourState = false, isSigned = false>
// CHECK-SAME: #obelisk_sim.dpi_abi<kind = chandle, direction = result, width = 64, fourState = false, isSigned = false>
// CHECK-SAME: obelisk_sim.dpi_c_identifier = "bounce"
// CHECK-NOT: obelisk.sv.
