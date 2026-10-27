// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

module {
  talos.sv.symbol.definition @s0.dpi_values attributes {definition_kind = 0 : i32, hierarchical_name = "dpi_values", name = "dpi_values", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.dpi_values attributes {hierarchical_name = "dpi_values", is_uninstantiated = false, name = "dpi_values", node_id = 3 : i64, referenced_path = "dpi_values", referenced_symbol = @s0.dpi_values} {
      talos.sv.symbol.instance_body @s4.dpi_values attributes {hierarchical_name = "dpi_values", name = "dpi_values", node_id = 4 : i64} {
        talos.sv.symbol.subroutine @s5.consume attributes {dpi_c_identifier = "consume", hierarchical_name = "dpi_values.consume", is_dpi_import, name = "consume", node_id = 5 : i64, semantic_type = !talos.subroutine<(!talos.string) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 6 : i64} {
          }
          talos.sv.symbol.formal_argument @s6.value attributes {direction = 0 : i32, hierarchical_name = "dpi_values.consume.value", name = "value", node_id = 7 : i64, semantic_type = !talos.string} {
          }
        }
        talos.sv.symbol.subroutine @s8.bounce attributes {dpi_c_identifier = "bounce", hierarchical_name = "dpi_values.bounce", is_dpi_import, name = "bounce", node_id = 8 : i64, semantic_type = !talos.subroutine<(!talos.chandle) -> !talos.chandle, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.list attributes {node_id = 9 : i64} {
          }
          talos.sv.symbol.formal_argument @s10.value attributes {direction = 0 : i32, hierarchical_name = "dpi_values.bounce.value", name = "value", node_id = 10 : i64, semantic_type = !talos.chandle} {
          }
        }
      }
    }
  }
}

// CHECK: simulation.code_unit.decl {{.*}} hierarchy "dpi_values.consume"
// CHECK-SAME: #dpi.abi<kind = string, direction = input, width = 64, fourState = false, isSigned = false>
// CHECK-SAME: dpi.c_identifier = "consume"
// CHECK: simulation.code_unit.decl {{.*}} hierarchy "dpi_values.bounce"
// CHECK-SAME: #dpi.abi<kind = chandle, direction = input, width = 64, fourState = false, isSigned = false>
// CHECK-SAME: #dpi.abi<kind = chandle, direction = result, width = 64, fourState = false, isSigned = false>
// CHECK-SAME: dpi.c_identifier = "bounce"
// CHECK-NOT: talos.sv.
