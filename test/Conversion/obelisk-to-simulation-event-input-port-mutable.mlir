// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// A read-only event input aliases its actual event cell. It therefore observes
// handle replacement without a startup propagation process.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "child", name = "child", node_id = 0 : i64, sym_name = "child"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "top_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "top_b", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.event, sym_name = "source"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.mutable", lifetime = 1 : i32, name = "mutable", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "mutable"} {
          obelisk.sv.expression.named_value attributes {node_id = 7 : i64, referenced_path = "top.source", referenced_symbol = @root::@top_i::@top_b::@source, semantic_type = !obelisk.event} {}
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.dut", is_uninstantiated = false, name = "dut", node_id = 8 : i64, referenced_path = "child", referenced_symbol = @child, sym_name = "dut"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "wake", formal_ordinal = 0 : i64, formal_path = "top.dut.wake", formal_symbol = @root::@top_i::@top_b::@dut::@child_b::@wake_p, formal_type = !obelisk.event, internal_path = "top.dut.wake", internal_symbol = @root::@top_i::@top_b::@dut::@child_b::@wake_v, is_ansi = true, is_net = false, node_id = 9 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "top.mutable", referenced_symbol = @root::@top_i::@top_b::@mutable, semantic_type = !obelisk.event} {}
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.dut", name = "child", node_id = 11 : i64, sym_name = "child_b", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.dut.wake", name = "wake", node_id = 12 : i64, semantic_type = !obelisk.event, sym_name = "wake_p"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.dut.wake", lifetime = 1 : i32, name = "wake", node_id = 13 : i64, semantic_type = !obelisk.event, sym_name = "wake_v"} {}
            obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top.dut", node_id = 14 : i64, procedure_kind = 0 : i32, sym_name = "wait", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 15 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 16 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.dut.wake", referenced_symbol = @root::@top_i::@top_b::@dut::@child_b::@wake_v, semantic_type = !obelisk.event} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 18 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.storage.decl {{.*}} : !obelisk_sim.event {{.*}} hierarchy "top.mutable"
// CHECK-NOT: hierarchy "top.dut.wake"
// CHECK-NOT: hierarchy "top.dut.$port_connection_0"
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.suspend.event
// CHECK-NOT: obelisk.sv.
