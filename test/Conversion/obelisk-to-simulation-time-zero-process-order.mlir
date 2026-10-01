// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.time_zero_process_order attributes {definition_kind = 0 : i32, hierarchical_name = "time_zero_process_order", name = "time_zero_process_order", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.time_zero_process_order attributes {hierarchical_name = "time_zero_process_order", is_uninstantiated = false, name = "time_zero_process_order", node_id = 3 : i64, referenced_path = "time_zero_process_order", referenced_symbol = @s0.time_zero_process_order} {
      obelisk.sv.symbol.instance_body @s4.time_zero_process_order attributes {hierarchical_name = "time_zero_process_order", name = "time_zero_process_order", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.source attributes {hierarchical_name = "time_zero_process_order.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {hierarchical_name = "time_zero_process_order", node_id = 6 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 7 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 8 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "time_zero_process_order.source", referenced_symbol = @s1.$root::@s3.time_zero_process_order::@s4.time_zero_process_order::@s5.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 10 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "time_zero_process_order", node_id = 11 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.empty attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.procedural_block @s13 attributes {hierarchical_name = "time_zero_process_order", node_id = 13 : i64, procedure_kind = 4 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.empty attributes {node_id = 14 : i64} {
          }
        }
        obelisk.sv.symbol.procedural_block @s15 attributes {hierarchical_name = "time_zero_process_order", node_id = 15 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.empty attributes {node_id = 16 : i64} {
          }
        }
      }
    }
  }
}

// An explicit always process starts first so its event control is armed before
// initial stimulus. IEEE 1800-2017 9.2.2.2 and 9.2.2.3 defer the automatic
// time-zero activations of always_comb and always_latch until after initial and
// always procedures have started.
// CHECK-LABEL: simulation.func @__obelisk_root
// CHECK: simulation.spawn @unit_0
// CHECK-NEXT: simulation.spawn @unit_3
// CHECK-NEXT: simulation.spawn @unit_1
// CHECK-NEXT: simulation.spawn @unit_2
// CHECK-NEXT: simulation.return
// CHECK-NOT: obelisk.sv.
