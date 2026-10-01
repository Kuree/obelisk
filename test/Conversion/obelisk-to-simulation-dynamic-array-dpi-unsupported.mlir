// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.dynamic_array_dpi_unsupported attributes {definition_kind = 0 : i32, hierarchical_name = "dynamic_array_dpi_unsupported", name = "dynamic_array_dpi_unsupported", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.dynamic_array_dpi_unsupported attributes {hierarchical_name = "dynamic_array_dpi_unsupported", is_uninstantiated = false, name = "dynamic_array_dpi_unsupported", node_id = 3 : i64, referenced_path = "dynamic_array_dpi_unsupported", referenced_symbol = @s0.dynamic_array_dpi_unsupported} {
      obelisk.sv.symbol.instance_body @s4.dynamic_array_dpi_unsupported attributes {hierarchical_name = "dynamic_array_dpi_unsupported", name = "dynamic_array_dpi_unsupported", node_id = 4 : i64} {
        obelisk.sv.symbol.subroutine @s5.consume attributes {dpi_c_identifier = "consume", hierarchical_name = "dynamic_array_dpi_unsupported.consume", is_dpi_import, name = "consume", node_id = 5 : i64, semantic_type = !obelisk.subroutine<(!obelisk.open_array<!obelisk.integral<32, true, false, 31 : 0, int>, false>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s6.value attributes {direction = 0 : i32, hierarchical_name = "dynamic_array_dpi_unsupported.consume.value", name = "value", node_id = 7 : i64, semantic_type = !obelisk.open_array<!obelisk.integral<32, true, false, 31 : 0, int>, false>} {
          }
        }
      }
    }
  }
}

// CHECK: #simulation.dpi_abi<kind = open_array
