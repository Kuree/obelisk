// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/clocking_block_event,/clocking_block_event, clocking_event_has_iff,/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=IFF

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s8.enable attributes {hierarchical_name = "top.enable", lifetime = 1 : i32, name = "enable", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.clocking_block @s6.cb attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 6 : i64} {}
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 8 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 9 : i64} {
              obelisk.sv.expression.arbitrary_symbol attributes {clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 10 : i64, referenced_path = "top.cb", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.cb, semantic_type = !obelisk.void} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 14 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.enable", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
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
// CHECK: simulation.context.storage
// CHECK: simulation.suspend.edge posedge
// CHECK-SAME: resume_region = 10 : i32
// CHECK-NOT: obelisk.sv.

// The declared iff condition is evaluated only on the primary edge. Arbitrary
// conditions use the observer path and still resume the waiter in Reactive.
// IFF-DAG: observer hierarchy "top.$code_unit_7.$observer.13.clocking_primary"
// IFF-DAG: observer hierarchy "top.$code_unit_7.$observer.14.clocking_iff"
// IFF-COUNT-2: simulation.observer.bind
// IFF: simulation.suspend.observe
// IFF-SAME: conditions 1 edges [1] indices [0]
// IFF-SAME: resume_region = 10 : i32
// IFF-NOT: obelisk.sv.
