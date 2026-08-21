// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/clocking_block_event,/clocking_block_event, clocking_event_has_iff,/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=IFF

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 6 : i64, sym_name = "s6.cb"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 8 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 9 : i64} {
              obelisk.sv.expression.arbitrary_symbol attributes {clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 10 : i64, referenced_path = "top.cb", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.cb, semantic_type = !obelisk.void} {}
            }
            obelisk.sv.statement.empty attributes {node_id = 11 : i64} {}
          }
        }
      }
    }
  }
}

// The void-typed clocking-block surface resolves to the captured clock
// storage, uses the edge declared by the clocking block, and resumes in the
// clocking event's Reactive synchronization region.
// CHECK: obelisk_sim.context.storage
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 10 : i32
// CHECK-NOT: obelisk.sv.

// IFF: clocking-block events with iff are not yet supported
