// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s \
// RUN:   --implicit-check-not=simulation.error \
// RUN:   --implicit-check-not=obelisk.sv.statement.wait_order

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "wait_order_failure_only", name = "wait_order_failure_only", node_id = 0 : i64, sym_name = "s0.wait_order_failure_only"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "wait_order_failure_only", is_uninstantiated = false, name = "wait_order_failure_only", node_id = 3 : i64, referenced_path = "wait_order_failure_only", referenced_symbol = @s0.wait_order_failure_only, sym_name = "s3.wait_order_failure_only"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "wait_order_failure_only", name = "wait_order_failure_only", node_id = 4 : i64, sym_name = "s4.wait_order_failure_only"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wait_order_failure_only.first", lifetime = 1 : i32, name = "first", node_id = 5 : i64, semantic_type = !obelisk.event, sym_name = "s5.first"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wait_order_failure_only.second", lifetime = 1 : i32, name = "second", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "s6.second"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "wait_order_failure_only", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.wait_order attributes {event_count = 2 : i64, has_failure_action = true, node_id = 8 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "wait_order_failure_only.first", referenced_symbol = @s1.$root::@s3.wait_order_failure_only::@s4.wait_order_failure_only::@s5.first, semantic_type = !obelisk.event} {
            }
            obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "wait_order_failure_only.second", referenced_symbol = @s1.$root::@s3.wait_order_failure_only::@s4.wait_order_failure_only::@s6.second, semantic_type = !obelisk.event} {
            }
            obelisk.sv.statement.empty attributes {node_id = 11 : i64} {
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 15.5.4 permits `[ statement ] else statement`: the absent
// success action is a no-op and the explicit failure action suppresses the
// default run-time error. With an empty failure statement, both outcome edges
// are no-ops and simplify to one continuation.
// CHECK: simulation.suspend.event_order %{{.*}}, %{{.*}} events 2 to ^[[DONE:bb[0-9]+]]
// CHECK: ^[[DONE]]:
// CHECK: simulation.return
