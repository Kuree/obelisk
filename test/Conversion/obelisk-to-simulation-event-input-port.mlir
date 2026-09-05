// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// A stable direct event input with an unwritten formal shares its actual's
// scheduler descriptor, so a child wait cannot race a time-zero handle copy.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "child", name = "child", node_id = 0 : i64, sym_name = "s0.child"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.wake", lifetime = 1 : i32, name = "wake", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "s6.wake"} {}
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.dut", is_uninstantiated = false, name = "dut", node_id = 7 : i64, referenced_path = "child", referenced_symbol = @s0.child, sym_name = "s7.dut"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "wake", formal_ordinal = 0 : i64, formal_path = "top.dut.wake", formal_symbol = @s2.$root::@s4.top::@s5.top::@s7.dut::@s8.child::@s9.wake, formal_type = !obelisk.event, internal_path = "top.dut.wake", internal_symbol = @s2.$root::@s4.top::@s5.top::@s7.dut::@s8.child::@s10.wake, is_ansi = true, is_net = false, node_id = 8 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "top.wake", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.wake, semantic_type = !obelisk.event} {}
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.dut", name = "child", node_id = 10 : i64, sym_name = "s8.child", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.dut.wake", name = "wake", node_id = 11 : i64, semantic_type = !obelisk.event, sym_name = "s9.wake"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.dut.wake", lifetime = 1 : i32, name = "wake", node_id = 12 : i64, semantic_type = !obelisk.event, sym_name = "s10.wake"} {}
            obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top.dut", node_id = 19 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 20 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 21 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.dut.wake", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.dut::@s8.child::@s10.wake, semantic_type = !obelisk.event} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 23 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}

// The formal retains a distinct read-only VPI identity while sharing the
// actual event's scheduler descriptor; no duplicate executable storage is
// introduced.
// CHECK-COUNT-1: obelisk_sim.vpi_object.anchor {{.*}} hierarchy "top.dut.wake"
// CHECK-NOT: obelisk_sim.storage.decl {{.*}} hierarchy "top.dut.wake"
// CHECK-NOT: hierarchy "top.dut.$port_connection_0"
// CHECK: !obelisk_sim.event {obelisk_sim.capture_kind = 6 : i32, obelisk_sim.descriptor_id = 0 : i64}
// CHECK: obelisk_sim.suspend.event
// CHECK-NOT: obelisk.sv.
