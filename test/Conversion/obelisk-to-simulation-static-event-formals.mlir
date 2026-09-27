// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 13.3.2 makes static-task formals persistent variables and
// initializes them to the type's default value. For event formals, 6.17 makes
// that value a fresh synchronization object. Copy-in, assignment, and copy-out
// then replace handles in those static cells. Per 13.5, an output argument is
// copied back on return, so an unused copy-in value need not cross the boundary.

// CHECK: simulation.storage.decl 2 in 1 : !simulation.event static hierarchy "event_formal.copy_event.incoming"
// CHECK: simulation.storage.decl 3 in 1 : !simulation.event static hierarchy "event_formal.copy_event.outgoing"
// CHECK-LABEL: simulation.func @__obelisk_root
// CHECK: %[[INCOMING:.*]] = simulation.context.storage %{{.*}}[2]
// CHECK: %[[INCOMING_FRESH:.*]] = simulation.event.create
// CHECK: simulation.ref.store %[[INCOMING_FRESH]] to %[[INCOMING]]
// CHECK: %[[OUTGOING:.*]] = simulation.context.storage %{{.*}}[3]
// CHECK: %[[OUTGOING_FRESH:.*]] = simulation.event.create
// CHECK: simulation.ref.store %[[OUTGOING_FRESH]] to %[[OUTGOING]]

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[STATIC_IN:.*]] = simulation.context.storage %arg0[2]
// CHECK: simulation.ref.store %arg1 to %[[STATIC_IN]]
// CHECK: %[[STATIC_OUT:.*]] = simulation.context.storage %arg0[3]
// CHECK: %[[COPIED:.*]] = simulation.ref.load %[[STATIC_IN]]
// CHECK: simulation.ref.store %[[COPIED]] to %[[STATIC_OUT]]
// CHECK: %[[RESULT:.*]] = simulation.ref.load %[[STATIC_OUT]]
// CHECK: simulation.ref.store %[[RESULT]] to %arg2

// CHECK-LABEL: simulation.func private @unit_1
// CHECK: %[[ACTUAL:.*]] = simulation.ref.load %arg1
// CHECK: simulation.task.call @unit_0(%arg0, %[[ACTUAL]], %arg2)
// CHECK-NOT: obelisk.sv.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "event_formal", name = "event_formal", node_id = 0 : i64, sym_name = "s0.event_formal"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "event_formal", is_uninstantiated = false, name = "event_formal", node_id = 3 : i64, referenced_path = "event_formal", referenced_symbol = @s0.event_formal, sym_name = "s3.event_formal"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "event_formal", name = "event_formal", node_id = 4 : i64, sym_name = "s4.event_formal", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "event_formal.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.event, sym_name = "s5.source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "event_formal.destination", lifetime = 1 : i32, name = "destination", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "s6.destination"} {
        }
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32, hierarchical_name = "event_formal.copy_event", name = "copy_event", node_id = 7 : i64, semantic_type = !obelisk.subroutine<(!obelisk.event, !obelisk.event) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s7.copy_event", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 9 : i64, semantic_type = !obelisk.event} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "event_formal.copy_event.outgoing", referenced_symbol = @s1.$root::@s3.event_formal::@s4.event_formal::@s7.copy_event::@s9.outgoing, semantic_type = !obelisk.event} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "event_formal.copy_event.incoming", referenced_symbol = @s1.$root::@s3.event_formal::@s4.event_formal::@s7.copy_event::@s8.incoming, semantic_type = !obelisk.event} {
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "event_formal.copy_event.incoming", lifetime = 1 : i32, name = "incoming", node_id = 12 : i64, semantic_type = !obelisk.event, sym_name = "s8.incoming"} {
            obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "event_formal.source", referenced_symbol = @s1.$root::@s3.event_formal::@s4.event_formal::@s5.source, semantic_type = !obelisk.event} {
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "event_formal.copy_event.outgoing", lifetime = 1 : i32, name = "outgoing", node_id = 13 : i64, semantic_type = !obelisk.event, sym_name = "s9.outgoing"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "event_formal", node_id = 14 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "copy_event", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 16 : i64, referenced_path = "event_formal.copy_event", referenced_symbol = @s1.$root::@s3.event_formal::@s4.event_formal::@s7.copy_event, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
              obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "event_formal.source", referenced_symbol = @s1.$root::@s3.event_formal::@s4.event_formal::@s5.source, semantic_type = !obelisk.event} {
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 18 : i64, semantic_type = !obelisk.event} {
                obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "event_formal.destination", referenced_symbol = @s1.$root::@s3.event_formal::@s4.event_formal::@s6.destination, semantic_type = !obelisk.event} {
                }
                obelisk.sv.expression.empty_argument attributes {node_id = 20 : i64, semantic_type = !obelisk.event} {
                }
              }
            }
          }
        }
      }
    }
  }
}
