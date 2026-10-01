// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 14.11: "If a clocking event has not yet occurred in the
// current time step, a ##0 cycle delay shall suspend the calling process until
// the clocking event occurs. When a process executes a ##0 cycle delay and the
// associated clocking event has already occurred in the current time step, the
// process shall continue execution without suspension." The leading ##0 here
// has no clocking event behind it and waits; the ##0 after the ##1 stays in
// that event's occurrence and falls through.
module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.clocking_block @s6.cb attributes {hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 6 : i64} {}
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 10 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 11 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 13 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 14 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 15 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 17 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 18 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 19 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 21 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-COUNT-2: simulation.suspend.edge posedge
// CHECK-NOT: simulation.suspend.edge
