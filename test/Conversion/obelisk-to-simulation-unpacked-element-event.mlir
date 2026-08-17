// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

// IEEE 1800-2017 9.4.2: an event expression may select a member of an
// aggregate as long as it reduces to a singular value, and only a change in
// that value is an event. The observer therefore captures the whole unpacked
// array along with the index and re-evaluates the selected element.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unpacked_element_event", name = "unpacked_element_event", node_id = 0 : i64, sym_name = "s0.unpacked_element_event"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unpacked_element_event", is_uninstantiated = false, name = "unpacked_element_event", node_id = 3 : i64, referenced_path = "unpacked_element_event", referenced_symbol = @s0.unpacked_element_event, sym_name = "s3.unpacked_element_event"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unpacked_element_event", name = "unpacked_element_event", node_id = 4 : i64, sym_name = "s4.unpacked_element_event", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unpacked_element_event.mem", lifetime = 1 : i32, name = "mem", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>, sym_name = "s5.mem"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unpacked_element_event.idx", lifetime = 1 : i32, name = "idx", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>, sym_name = "s6.idx"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unpacked_element_event.out", lifetime = 1 : i32, name = "out", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>, sym_name = "s7.out"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unpacked_element_event", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 10 : i64} {
              obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "unpacked_element_event.mem", referenced_symbol = @s1.$root::@s3.unpacked_element_event::@s4.unpacked_element_event::@s5.mem, semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "unpacked_element_event.idx", referenced_symbol = @s1.$root::@s3.unpacked_element_event::@s4.unpacked_element_event::@s6.idx, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "unpacked_element_event.out", referenced_symbol = @s1.$root::@s3.unpacked_element_event::@s4.unpacked_element_event::@s7.out, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                }
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "unpacked_element_event.mem", referenced_symbol = @s1.$root::@s3.unpacked_element_event::@s4.unpacked_element_event::@s5.mem, semantic_type = !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "unpacked_element_event.idx", referenced_symbol = @s1.$root::@s3.unpacked_element_event::@s4.unpacked_element_event::@s6.idx, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
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
// CHECK-SAME: %[[MEM:[^:]*]]: !obelisk_sim.ref<!obelisk_sim.unpacked_array<3 : 0 x !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>>
// CHECK-SAME: %[[IDX:[^:]*]]: !obelisk_sim.ref<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
// CHECK: %[[OBSERVER:.*]] = obelisk_sim.observer.bind @observer_
// CHECK-SAME: values(%[[MEM]], %[[IDX]], %[[MEM]], %[[IDX]]
// CHECK-SAME: captures 2 : <!obelisk_sim.logic<8>>
// CHECK: obelisk_sim.suspend.observe %[[OBSERVER]],

// CHECK: obelisk_sim.func private @observer_
// CHECK: %[[ELEMENT:.*]] = obelisk_sim.ref.array_element
// CHECK: obelisk_sim.ref.load %[[ELEMENT]]
