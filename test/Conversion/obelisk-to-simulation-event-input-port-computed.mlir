// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "child", name = "child", node_id = 0 : i64, sym_name = "child"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "top_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "top_b", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.select", lifetime = 1 : i32, name = "select", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "select"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.first", lifetime = 1 : i32, name = "first", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "first"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.second", lifetime = 1 : i32, name = "second", node_id = 7 : i64, semantic_type = !obelisk.event, sym_name = "second"} {}
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.dut", is_uninstantiated = false, name = "dut", node_id = 8 : i64, referenced_path = "child", referenced_symbol = @child, sym_name = "dut"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "wake", formal_ordinal = 0 : i64, formal_path = "top.dut.wake", formal_symbol = @root::@top_i::@top_b::@dut::@child_b::@wake_p, formal_type = !obelisk.event, internal_path = "top.dut.wake", internal_symbol = @root::@top_i::@top_b::@dut::@child_b::@wake_v, is_ansi = true, is_net = false, node_id = 9 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 10 : i64, semantic_type = !obelisk.event} {
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.select", referenced_symbol = @root::@top_i::@top_b::@select, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.first", referenced_symbol = @root::@top_i::@top_b::@first, semantic_type = !obelisk.event} {}
              obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.second", referenced_symbol = @root::@top_i::@top_b::@second, semantic_type = !obelisk.event} {}
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.dut", name = "child", node_id = 14 : i64, sym_name = "child_b", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.dut.wake", name = "wake", node_id = 15 : i64, semantic_type = !obelisk.event, sym_name = "wake_p"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.dut.wake", lifetime = 1 : i32, name = "wake", node_id = 16 : i64, semantic_type = !obelisk.event, sym_name = "wake_v"} {}
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.design @design attributes {{.*}}obelisk_sim.computed_event_startup
// CHECK: obelisk_sim.storage.decl {{.*}} : !obelisk_sim.event {{.*}} hierarchy "top.dut.wake"
// CHECK: obelisk_sim.func private @unit_0
// CHECK-SAME: entry_kind = 9 : i32
// CHECK-SAME: obelisk_sim.computed_event_startup
// CHECK: %[[SAME:.*]] = obelisk_sim.event.equal %[[TRUE:.*]], %[[FALSE:.*]]
// CHECK: %[[NULL:.*]] = obelisk_sim.event.null
// CHECK: arith.select %[[SAME]], %[[TRUE]], %[[NULL]]
// CHECK: obelisk_sim.ref.store
// CHECK: obelisk_sim.suspend.change
// CHECK-NOT: obelisk.sv.
