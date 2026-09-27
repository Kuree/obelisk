// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' | FileCheck %s --check-prefix=BYTECODE

// IEEE 1800-2017 6.17 and 15.5.5: named events are assignable handles.
// Explicit initialization aliases another synchronization object or stores
// null, while an uninitialized mutable event receives one fresh object.
// A wait captures the handle before suspension, so later cell assignment does
// not migrate that already-suspended wait to the replacement object.

// CHECK: simulation.storage.decl 0 in 1 : !simulation.event design hierarchy "event_cell.mutable"
// CHECK: simulation.storage.decl 1 in 1 : !simulation.event design hierarchy "event_cell.alias"
// CHECK-SAME: simulation.event_explicit_initializer
// CHECK: simulation.storage.decl 2 in 1 : !simulation.event design hierarchy "event_cell.empty"
// CHECK-SAME: simulation.event_explicit_initializer
// CHECK-LABEL: simulation.func @__obelisk_root
// CHECK: %[[MUTABLE:.*]] = simulation.context.storage %{{.*}}[0]
// CHECK: %[[FRESH:.*]] = simulation.event.create
// CHECK: simulation.ref.store %[[FRESH]] to %[[MUTABLE]]
// CHECK: %[[SOURCE:.*]] = simulation.context.event %{{.*}}[0]
// CHECK: simulation.call @unit_0(%{{.*}}, %[[SOURCE]])
// CHECK: simulation.call @unit_1

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[ALIAS:.*]] = simulation.context.storage %{{.*}}[1]
// CHECK: simulation.ref.store %arg1 to %[[ALIAS]]

// CHECK-LABEL: simulation.func private @unit_1
// CHECK: %[[NULL:.*]] = simulation.event.null
// CHECK: %[[EMPTY:.*]] = simulation.context.storage %{{.*}}[2]
// CHECK: simulation.ref.store %[[NULL]] to %[[EMPTY]]

// CHECK-LABEL: simulation.func private @unit_2
// CHECK: %[[WAITED:.*]] = simulation.ref.load %arg1
// CHECK-NEXT: simulation.suspend.event %[[WAITED]]

// CHECK-LABEL: simulation.func private @unit_3
// CHECK: %[[ASSIGNNULL:.*]] = simulation.event.null
// CHECK: simulation.ref.store %arg3 to %arg1
// CHECK: simulation.ref.store %[[ASSIGNNULL]] to %arg2
// CHECK: simulation.ref.store %arg3 to %arg2
// CHECK: %[[TRIGGER:.*]] = simulation.ref.load %arg2
// CHECK-NEXT: simulation.event.trigger %[[TRIGGER]]
// CHECK-NOT: obelisk.sv.

// NATIVE: llvm.func @obelisk_rt_v1_scheduler_event_create
// NATIVE: llvm.func @obelisk_rt_v1_scheduler_event(
// BYTECODE: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "event_cell", name = "event_cell", node_id = 0 : i64, sym_name = "s0.event_cell"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "event_cell", is_uninstantiated = false, name = "event_cell", node_id = 3 : i64, referenced_path = "event_cell", referenced_symbol = @s0.event_cell, sym_name = "s3.event_cell"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "event_cell", name = "event_cell", node_id = 4 : i64, sym_name = "s4.event_cell", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "event_cell.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.event, sym_name = "s5.source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "event_cell.mutable", lifetime = 1 : i32, name = "mutable", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "s6.mutable"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "event_cell.alias", lifetime = 1 : i32, name = "alias", node_id = 7 : i64, semantic_type = !obelisk.event, sym_name = "s7.alias"} {
          obelisk.sv.expression.named_value attributes {node_id = 8 : i64, referenced_path = "event_cell.source", referenced_symbol = @s1.$root::@s3.event_cell::@s4.event_cell::@s5.source, semantic_type = !obelisk.event} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "event_cell.empty", lifetime = 1 : i32, name = "empty", node_id = 9 : i64, semantic_type = !obelisk.event, sym_name = "s8.empty"} {
          obelisk.sv.expression.conversion attributes {node_id = 10 : i64, semantic_type = !obelisk.event} {
            obelisk.sv.expression.null_literal attributes {node_id = 11 : i64, semantic_type = !obelisk.null} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "event_cell", node_id = 12 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 13 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 14 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "event_cell.alias", referenced_symbol = @s1.$root::@s3.event_cell::@s4.event_cell::@s7.alias, semantic_type = !obelisk.event} {
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 16 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "event_cell", node_id = 17 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 18 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, semantic_type = !obelisk.event} {
                obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "event_cell.mutable", referenced_symbol = @s1.$root::@s3.event_cell::@s4.event_cell::@s6.mutable, semantic_type = !obelisk.event} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "event_cell.source", referenced_symbol = @s1.$root::@s3.event_cell::@s4.event_cell::@s5.source, semantic_type = !obelisk.event} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 24 : i64, semantic_type = !obelisk.event} {
                obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "event_cell.alias", referenced_symbol = @s1.$root::@s3.event_cell::@s4.event_cell::@s7.alias, semantic_type = !obelisk.event} {
                }
                obelisk.sv.expression.conversion attributes {node_id = 26 : i64, semantic_type = !obelisk.event} {
                  obelisk.sv.expression.null_literal attributes {node_id = 27 : i64, semantic_type = !obelisk.null} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 29 : i64, semantic_type = !obelisk.event} {
                obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "event_cell.alias", referenced_symbol = @s1.$root::@s3.event_cell::@s4.event_cell::@s7.alias, semantic_type = !obelisk.event} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "event_cell.source", referenced_symbol = @s1.$root::@s3.event_cell::@s4.event_cell::@s5.source, semantic_type = !obelisk.event} {
                }
              }
            }
            obelisk.sv.statement.event_trigger attributes {node_id = 32 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "event_cell.alias", referenced_symbol = @s1.$root::@s3.event_cell::@s4.event_cell::@s7.alias, semantic_type = !obelisk.event} {
              }
            }
          }
        }
      }
    }
  }
}
