// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_wait_order", name = "unsupported_wait_order", node_id = 0 : i64, sym_name = "s0.unsupported_wait_order"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_wait_order", is_uninstantiated = false, name = "unsupported_wait_order", node_id = 3 : i64, referenced_path = "unsupported_wait_order", referenced_symbol = @s0.unsupported_wait_order, sym_name = "s3.unsupported_wait_order"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_wait_order", name = "unsupported_wait_order", node_id = 4 : i64, sym_name = "s4.unsupported_wait_order"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_wait_order.first", lifetime = 1 : i32, name = "first", node_id = 5 : i64, semantic_type = !obelisk.event, sym_name = "s5.first"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_wait_order.second", lifetime = 1 : i32, name = "second", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "s6.second"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_wait_order", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.wait_order attributes {event_count = 2 : i64, has_success_action = true, node_id = 8 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "unsupported_wait_order.first", referenced_symbol = @s1.$root::@s3.unsupported_wait_order::@s4.unsupported_wait_order::@s5.first, semantic_type = !obelisk.event} {
            }
            obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "unsupported_wait_order.second", referenced_symbol = @s1.$root::@s3.unsupported_wait_order::@s4.unsupported_wait_order::@s6.second, semantic_type = !obelisk.event} {
            }
            obelisk.sv.statement.empty attributes {node_id = 11 : i64} {
            }
          }
        }
      }
    }
  }
}

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: wait_order occurrence sequencing
