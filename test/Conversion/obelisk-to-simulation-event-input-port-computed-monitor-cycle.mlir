// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition @leaf attributes {definition_kind = 0 : i32, name = "leaf", node_id = 0 : i64} {}
  obelisk.sv.symbol.definition @mid attributes {definition_kind = 0 : i32, name = "mid", node_id = 1 : i64} {}
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, name = "top", node_id = 2 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 3 : i64} {
    obelisk.sv.symbol.instance @top_i attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @top} {
      obelisk.sv.symbol.instance_body @top_b attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.variable @source attributes {hierarchical_name = "top.source", lifetime = 1 : i32, name = "source", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 7 : i64, semantic_type = !obelisk.event} {}
        obelisk.sv.symbol.variable @b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 8 : i64, semantic_type = !obelisk.event} {}
        obelisk.sv.symbol.instance @m attributes {hierarchical_name = "top.m", name = "m", node_id = 9 : i64, referenced_path = "mid", referenced_symbol = @mid} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "sel", formal_ordinal = 0 : i64, formal_path = "top.m.sel", formal_symbol = @root::@top_i::@top_b::@m::@mid_b::@sel_p, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "top.m.sel", internal_symbol = @root::@top_i::@top_b::@m::@mid_b::@sel_v, is_ansi = true, is_net = false, node_id = 10 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.source", referenced_symbol = @root::@top_i::@top_b::@source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
          obelisk.sv.symbol.instance_body @mid_b attributes {hierarchical_name = "top.m", name = "mid", node_id = 12 : i64, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
            obelisk.sv.symbol.port @sel_p attributes {direction = 0 : i32, hierarchical_name = "top.m.sel", name = "sel", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.symbol.variable @sel_v attributes {hierarchical_name = "top.m.sel", lifetime = 1 : i32, name = "sel", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.symbol.instance @l attributes {hierarchical_name = "top.m.l", name = "l", node_id = 15 : i64, referenced_path = "leaf", referenced_symbol = @leaf} {
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "wake", formal_ordinal = 0 : i64, formal_path = "top.m.l.wake", formal_symbol = @root::@top_i::@top_b::@m::@mid_b::@l::@leaf_b::@wake_p, formal_type = !obelisk.event, internal_path = "top.m.l.wake", internal_symbol = @root::@top_i::@top_b::@m::@mid_b::@l::@leaf_b::@wake_v, is_ansi = true, is_net = false, node_id = 16 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 17 : i64, semantic_type = !obelisk.event} {
                  obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.m.sel", referenced_symbol = @root::@top_i::@top_b::@m::@mid_b::@sel_v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.hierarchical_value attributes {node_id = 19 : i64, referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_b::@a, semantic_type = !obelisk.event} {}
                  obelisk.sv.expression.hierarchical_value attributes {node_id = 20 : i64, referenced_path = "top.b", referenced_symbol = @root::@top_i::@top_b::@b, semantic_type = !obelisk.event} {}
                }
              }
              obelisk.sv.symbol.instance_body @leaf_b attributes {hierarchical_name = "top.m.l", name = "leaf", node_id = 21 : i64, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
                obelisk.sv.symbol.port @wake_p attributes {direction = 0 : i32, hierarchical_name = "top.m.l.wake", name = "wake", node_id = 22 : i64, semantic_type = !obelisk.event} {}
                obelisk.sv.symbol.variable @wake_v attributes {hierarchical_name = "top.m.l.wake", lifetime = 1 : i32, name = "wake", node_id = 23 : i64, semantic_type = !obelisk.event} {}
              }
            }
            obelisk.sv.symbol.clocking_block @cb attributes {clocking_event_list, clocking_event_monitor, hierarchical_name = "top.m.cb", is_default = true, is_global = false, name = "cb", node_id = 24 : i64} {
              obelisk.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 25 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 26 : i64} {
                  obelisk.sv.expression.hierarchical_value attributes {node_id = 27 : i64, referenced_path = "top.m.l.wake", referenced_symbol = @root::@top_i::@top_b::@m::@mid_b::@l::@leaf_b::@wake_v, semantic_type = !obelisk.event} {}
                }
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 28 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_path = "top.m.sel", referenced_symbol = @root::@top_i::@top_b::@m::@mid_b::@sel_v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: computed event input startup dependency is cyclic through this event control
