// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

//   module source(output wire value[2]); endmodule
//   module top; interconnect n[2]; source d(n); endmodule
//
// IEEE 1800-2023 6.6.8: each leaf of an interconnect array is its own net and
// takes the type of the port element it connects to. The unpacked-array port
// therefore pairs with the leaves element by element (23.3.3), not across its
// padded storage.

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.source attributes {definition_kind = 0 : i32, hierarchical_name = "source", name = "source", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.definition @s1.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {
  }
  obelisk.sv.symbol.root @s2.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.compilation_unit @s3 attributes {hierarchical_name = "$unit", node_id = 3 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s4.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top} {
      obelisk.sv.symbol.instance_body @s5.top attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.net @s6.n attributes {hierarchical_name = "top.n", is_implicit = false, name = "n", net_kind = 13 : i32, node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.untyped>} {
        }
        obelisk.sv.symbol.instance @s7.d attributes {hierarchical_name = "top.d", is_uninstantiated = false, name = "d", node_id = 7 : i64, referenced_path = "source", referenced_symbol = @s0.source} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "value", formal_ordinal = 0 : i64, formal_path = "top.d.value", formal_symbol = @s2.$root::@s4.top::@s5.top::@s7.d::@s8.source::@s9.value, formal_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<1, false, true, 0 : 0, logic>>, internal_path = "top.d.value", internal_symbol = @s2.$root::@s4.top::@s5.top::@s7.d::@s8.source::@s10.value, is_ansi = true, is_net = true, node_id = 8 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.untyped>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "top.n", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.n, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.untyped>} {
              }
              obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body @s8.source attributes {hierarchical_name = "top.d", name = "source", node_id = 12 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "source", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
            obelisk.sv.symbol.port @s9.value attributes {direction = 1 : i32, hierarchical_name = "top.d.value", name = "value", node_id = 13 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            }
            obelisk.sv.symbol.net @s10.value attributes {hierarchical_name = "top.d.value", is_implicit = false, name = "value", net_kind = 1 : i32, node_id = 14 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            }
          }
        }
      }
    }
  }
}


// CHECK: simulation.net.decl [[N0:[0-9]+]] in {{[0-9]+}} : !simulation.logic<1> design hierarchy "top.n[0]"
// CHECK: simulation.net.decl [[N1:[0-9]+]] in {{[0-9]+}} : !simulation.logic<1> design hierarchy "top.n[1]"
// CHECK: simulation.net.decl [[VALUE:[0-9]+]] in {{[0-9]+}} : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design hierarchy "top.d.value"
// CHECK: simulation.net.connect.decl {{[0-9]+}} in {{[0-9]+}} [[N0]][0] to [[VALUE]][0] width 1
// CHECK: simulation.net.connect.decl {{[0-9]+}} in {{[0-9]+}} [[N1]][0] to [[VALUE]][1] width 1
