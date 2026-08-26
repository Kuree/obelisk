// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, name = "child", node_id = 0 : i64, sym_name = "child"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, name = "top", node_id = 1 : i64, sym_name = "top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "top_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "top_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.select", lifetime = 1 : i32, name = "select", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "select"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.other", lifetime = 1 : i32, name = "other", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "other"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 7 : i64, semantic_type = !obelisk.event, sym_name = "a"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 8 : i64, semantic_type = !obelisk.event, sym_name = "b"} {}
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "top", node_id = 9 : i64, sym_name = "to_select", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.select", referenced_symbol = @root::@top_i::@top_b::@select, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.other", referenced_symbol = @root::@top_i::@top_b::@other, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        }
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "top", node_id = 13 : i64, sym_name = "to_other", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "top.other", referenced_symbol = @root::@top_i::@top_b::@other, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.select", referenced_symbol = @root::@top_i::@top_b::@select, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.dut", name = "dut", node_id = 17 : i64, referenced_path = "child", referenced_symbol = @child, sym_name = "dut"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "wake", formal_ordinal = 0 : i64, formal_path = "top.dut.wake", formal_symbol = @root::@top_i::@top_b::@dut::@child_b::@wake_p, formal_type = !obelisk.event, internal_path = "top.dut.wake", internal_symbol = @root::@top_i::@top_b::@dut::@child_b::@wake_v, is_ansi = true, is_net = false, node_id = 18 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 19 : i64, semantic_type = !obelisk.event} {
              obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "top.select", referenced_symbol = @root::@top_i::@top_b::@select, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_b::@a, semantic_type = !obelisk.event} {}
              obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.b", referenced_symbol = @root::@top_i::@top_b::@b, semantic_type = !obelisk.event} {}
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.dut", name = "child", node_id = 23 : i64, sym_name = "child_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.dut.wake", name = "wake", node_id = 24 : i64, semantic_type = !obelisk.event, sym_name = "wake_p"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.dut.wake", lifetime = 1 : i32, name = "wake", node_id = 25 : i64, semantic_type = !obelisk.event, sym_name = "wake_v"} {}
          }
        }
      }
    }
  }
}

// CHECK: computed event input startup dependency is cyclic
