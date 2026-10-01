// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s \
// RUN:   --implicit-check-not=obelisk.sv.statement.wait_order

module {
  obelisk.sv.symbol.definition @s0.wait_order_default_failure attributes {definition_kind = 0 : i32, hierarchical_name = "wait_order_default_failure", name = "wait_order_default_failure", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.wait_order_default_failure attributes {hierarchical_name = "wait_order_default_failure", is_uninstantiated = false, name = "wait_order_default_failure", node_id = 3 : i64, referenced_path = "wait_order_default_failure", referenced_symbol = @s0.wait_order_default_failure} {
      obelisk.sv.symbol.instance_body @s4.wait_order_default_failure attributes {hierarchical_name = "wait_order_default_failure", name = "wait_order_default_failure", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.first attributes {hierarchical_name = "wait_order_default_failure.first", lifetime = 1 : i32, name = "first", node_id = 5 : i64, semantic_type = !obelisk.event} {
        }
        obelisk.sv.symbol.variable @s6.second attributes {hierarchical_name = "wait_order_default_failure.second", lifetime = 1 : i32, name = "second", node_id = 6 : i64, semantic_type = !obelisk.event} {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "wait_order_default_failure", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.wait_order attributes {event_count = 2 : i64, has_success_action = true, node_id = 8 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "wait_order_default_failure.first", referenced_symbol = @s1.$root::@s3.wait_order_default_failure::@s4.wait_order_default_failure::@s5.first, semantic_type = !obelisk.event} {
            }
            obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "wait_order_default_failure.second", referenced_symbol = @s1.$root::@s3.wait_order_default_failure::@s4.wait_order_default_failure::@s6.second, semantic_type = !obelisk.event} {
            }
            obelisk.sv.statement.empty attributes {node_id = 11 : i64} {
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 15.5.4: the event inventory is one scheduler wait. The
// resumed result selects the action block, and omission of an else statement
// makes only the failure edge record a run-time error.
// CHECK: simulation.suspend.event_order %{{.*}}, %{{.*}} events 2 to
// CHECK: %[[FAILED:.*]] = simulation.wait_order.failed
// CHECK: cf.cond_br %[[FAILED]], ^[[FAIL:bb[0-9]+]], ^[[DONE:bb[0-9]+]]
// CHECK: ^[[FAIL]]:
// CHECK: simulation.error
// CHECK: cf.br ^[[DONE]]
