// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 15.5.1: `->>` with an event control does not block the
// caller; once the control occurs, the event is triggered in the NBA region.
// A repeat count is evaluated when the statement executes (9.4.5).
//
// module top;
//   event e; bit clk;
//   initial begin
//     ->> @(posedge clk) e;
//     ->> repeat(2) @(posedge clk) e;
//   end
// endmodule

// CHECK-LABEL: simulation.func private @unit_0.$nba_event.
// CHECK-SAME: %arg1: !simulation.event
// CHECK: simulation.suspend.edge posedge %arg2 to ^[[FIRE:bb[0-9]+]]
// CHECK: ^[[FIRE]]:
// CHECK-NEXT: simulation.event.trigger %arg1 nonblocking = true

// CHECK-LABEL: simulation.func private @unit_0.$nba_event.
// CHECK-SAME: %arg1: !simulation.event
// CHECK-SAME: %arg2: i64
// CHECK: simulation.event.trigger %arg1 nonblocking = true
// CHECK: simulation.suspend.edge posedge %arg3

// The caller only spawns the deferred triggers.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[TWO:.+]] = arith.constant 2 : i64
// CHECK-NEXT: simulation.spawn @unit_0.$nba_event.{{[0-9]+}}(%arg0, %arg2, %arg1)
// CHECK-NEXT: simulation.spawn @unit_0.$nba_event.{{[0-9]+}}(%arg0, %arg2, %[[TWO]], %arg1)
// CHECK-NEXT: simulation.return

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.e attributes {hierarchical_name = "top.e", lifetime = 1 : i32, name = "e", node_id = 5 : i64, semantic_type = !obelisk.event} {
        }
        obelisk.sv.symbol.variable @s6.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.event_trigger attributes {has_timing_control = true, is_nonblocking = true, node_id = 10 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.e", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.e, semantic_type = !obelisk.event} {
                }
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
              }
              obelisk.sv.statement.event_trigger attributes {has_timing_control = true, is_nonblocking = true, node_id = 14 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.e", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.e, semantic_type = !obelisk.event} {
                }
                obelisk.sv.timing.repeated_event attributes {node_id = 16 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
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
}

