// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s \
// RUN:   --implicit-check-not=simulation.error \
// RUN:   --implicit-check-not=obelisk.sv.statement.wait_order

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "wait_order_actions", name = "wait_order_actions", node_id = 0 : i64, sym_name = "s0.wait_order_actions"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "wait_order_actions", is_uninstantiated = false, name = "wait_order_actions", node_id = 3 : i64, referenced_path = "wait_order_actions", referenced_symbol = @s0.wait_order_actions, sym_name = "s3.wait_order_actions"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "wait_order_actions", name = "wait_order_actions", node_id = 4 : i64, sym_name = "s4.wait_order_actions"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wait_order_actions.first", lifetime = 1 : i32, name = "first", node_id = 5 : i64, semantic_type = !obelisk.event, sym_name = "s5.first"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wait_order_actions.second", lifetime = 1 : i32, name = "second", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "s6.second"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wait_order_actions.result", lifetime = 1 : i32, name = "result", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "wait_order_actions", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.wait_order attributes {event_count = 2 : i64, has_failure_action = true, has_success_action = true, node_id = 9 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "wait_order_actions.first", referenced_symbol = @s1.$root::@s3.wait_order_actions::@s4.wait_order_actions::@s5.first, semantic_type = !obelisk.event} {
            }
            obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "wait_order_actions.second", referenced_symbol = @s1.$root::@s3.wait_order_actions::@s4.wait_order_actions::@s6.second, semantic_type = !obelisk.event} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "wait_order_actions.result", referenced_symbol = @s1.$root::@s3.wait_order_actions::@s4.wait_order_actions::@s7.result, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "wait_order_actions.result", referenced_symbol = @s1.$root::@s3.wait_order_actions::@s4.wait_order_actions::@s7.result, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 15.5.4: success executes the first action and an explicit
// failure action executes instead of the default run-time error.
// CHECK: simulation.suspend.event_order %{{.*}}, %{{.*}} events 2 to
// CHECK: %[[FAILED:.*]] = simulation.wait_order.failed
// CHECK: cf.cond_br %[[FAILED]], ^[[FAIL:bb[0-9]+]], ^[[SUCCESS:bb[0-9]+]]
// CHECK: ^[[SUCCESS]]:
// CHECK: simulation.logic.constant true, false
// CHECK: simulation.ref.store
// CHECK: cf.br ^[[DONE:bb[0-9]+]]
// CHECK: ^[[FAIL]]:
// CHECK: simulation.logic.constant false, false
// CHECK: simulation.ref.store
// CHECK: cf.br ^[[DONE]]
