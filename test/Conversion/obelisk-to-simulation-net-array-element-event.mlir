// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

// IEEE 1800-2017 9.4.2: an event expression may select an element of an
// aggregate as long as it reduces to a singular value, and only a change in
// that value is an event. A built-in net exposes only flat packed windows of
// its storage, so an element of an unpacked array of nets has no watchable
// handle. The event is therefore evaluated by an observer, which re-reads the
// whole net and selects the element from the value it read.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "net_array_element_event", name = "net_array_element_event", node_id = 0 : i64, sym_name = "s0.net_array_element_event"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "net_array_element_event", is_uninstantiated = false, name = "net_array_element_event", node_id = 3 : i64, referenced_path = "net_array_element_event", referenced_symbol = @s0.net_array_element_event, sym_name = "s3.net_array_element_event"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "net_array_element_event", name = "net_array_element_event", node_id = 4 : i64, sym_name = "s4.net_array_element_event", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_array_element_event.n", is_implicit = false, name = "n", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s5.n"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "net_array_element_event.out", lifetime = 1 : i32, name = "out", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>, sym_name = "s6.out"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "net_array_element_event", node_id = 7 : i64, procedure_kind = 2 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 8 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 9 : i64} {
              obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "net_array_element_event.n", referenced_symbol = @s1.$root::@s3.net_array_element_event::@s4.net_array_element_event::@s5.n, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "net_array_element_event.out", referenced_symbol = @s1.$root::@s3.net_array_element_event::@s4.net_array_element_event::@s6.out, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                }
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "net_array_element_event.n", referenced_symbol = @s1.$root::@s3.net_array_element_event::@s4.net_array_element_event::@s5.n, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: obelisk_sim.func private @unit_0(
// CHECK-SAME: %[[NET:[^:]*]]: !obelisk_sim.net<!obelisk_sim.unpacked_array<1 : 0 x !obelisk_sim.packed_array<3 : 0 x !obelisk_sim.logic<1>>>>
// CHECK: %[[OBSERVER:.*]] = obelisk_sim.observer.bind @observer_
// CHECK-SAME: values(%[[NET]]
// CHECK: obelisk_sim.suspend.observe %[[OBSERVER]],

// CHECK: obelisk_sim.func private @observer_
// CHECK: %[[VALUE:.*]] = obelisk_sim.net.read
// CHECK: obelisk_sim.aggregate.extract %[[VALUE]]
