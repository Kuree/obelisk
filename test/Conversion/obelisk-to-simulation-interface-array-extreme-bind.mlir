// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Singleton fixed ranges at either signed endpoint are legal. Lowering must
// bind the exact indexed scope without incrementing beyond the final element.

!vif = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.maximum::@s7, "">
!maximum = !obelisk.ranged_unpacked_array<9223372036854775807 : 9223372036854775807 x !vif>
!minimum = !obelisk.ranged_unpacked_array<-9223372036854775808 : -9223372036854775808 x !vif>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bind_if", name = "bind_if", node_id = 0 : i64, sym_name = "s0.bind_if"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance_array attributes {hierarchical_name = "top.maximum", name = "maximum", node_id = 6 : i64, sym_name = "s6.maximum"} {
          obelisk.sv.symbol.instance attributes {hierarchical_name = "top.maximum[9223372036854775807]", is_uninstantiated = false, node_id = 7 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s7"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.maximum[9223372036854775807]", name = "bind_if", node_id = 8 : i64, sym_name = "s8.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.maximum::@s7} {}
          }
        }
        obelisk.sv.symbol.instance_array attributes {hierarchical_name = "top.minimum", name = "minimum", node_id = 9 : i64, sym_name = "s9.minimum"} {
          obelisk.sv.symbol.instance attributes {hierarchical_name = "top.minimum[-9223372036854775808]", is_uninstantiated = false, node_id = 10 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s10"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.minimum[-9223372036854775808]", name = "bind_if", node_id = 11 : i64, sym_name = "s11.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.maximum::@s7} {}
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.maximum_handle", lifetime = 1 : i32, name = "maximum_handle", node_id = 12 : i64, semantic_type = !maximum, sym_name = "s12.maximum_handle"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.minimum_handle", lifetime = 1 : i32, name = "minimum_handle", node_id = 13 : i64, semantic_type = !minimum, sym_name = "s13.minimum_handle"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 14 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 15 : i64} {
            obelisk.sv.statement.list attributes {node_id = 16 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 18 : i64, semantic_type = !maximum} {
                  obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.maximum_handle", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.maximum_handle, semantic_type = !maximum} {}
                  obelisk.sv.expression.arbitrary_symbol attributes {node_id = 20 : i64, referenced_path = "top.maximum", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.maximum, semantic_type = !maximum} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 22 : i64, semantic_type = !minimum} {
                  obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.minimum_handle", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.minimum_handle, semantic_type = !minimum} {}
                  obelisk.sv.expression.arbitrary_symbol attributes {node_id = 24 : i64, referenced_path = "top.minimum", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s9.minimum, semantic_type = !minimum} {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.virtual_interface.bind 2
// CHECK: obelisk_sim.aggregate.construct
// CHECK: obelisk_sim.virtual_interface.bind 3
// CHECK: obelisk_sim.aggregate.construct
// CHECK-NOT: obelisk_sim.container
// CHECK-NOT: obelisk.sv.
