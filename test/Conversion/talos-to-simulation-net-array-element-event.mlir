// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' \
// RUN:   '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' \
// RUN:   --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null

// IEEE 1800-2017 9.4.2: an event expression may select an element of an
// aggregate as long as it reduces to a singular value, and only a change in
// that value is an event. An event on an element of an unpacked array of nets
// is evaluated by an observer, which reads only that element through an exact
// element view of the net.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  talos.sv.symbol.definition @s0.net_array_element_event attributes {definition_kind = 0 : i32, hierarchical_name = "net_array_element_event", name = "net_array_element_event", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.net_array_element_event attributes {hierarchical_name = "net_array_element_event", is_uninstantiated = false, name = "net_array_element_event", node_id = 3 : i64, referenced_path = "net_array_element_event", referenced_symbol = @s0.net_array_element_event} {
      talos.sv.symbol.instance_body @s4.net_array_element_event attributes {hierarchical_name = "net_array_element_event", name = "net_array_element_event", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.net @s5.n attributes {hierarchical_name = "net_array_element_event.n", is_implicit = false, name = "n", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !talos.ranged_unpacked_array<1 : 0 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
        }
        talos.sv.symbol.variable @s6.out attributes {hierarchical_name = "net_array_element_event.out", lifetime = 1 : i32, name = "out", node_id = 6 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, reg>>} {
        }
        talos.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "net_array_element_event", node_id = 7 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.timed attributes {node_id = 8 : i64} {
            talos.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 9 : i64} {
              talos.sv.expression.element_select attributes {is_signed = false, node_id = 10 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "net_array_element_event.n", referenced_symbol = @s1.$root::@s3.net_array_element_event::@s4.net_array_element_event::@s5.n, semantic_type = !talos.ranged_unpacked_array<1 : 0 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
                }
                talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            talos.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, reg>>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "net_array_element_event.out", referenced_symbol = @s1.$root::@s3.net_array_element_event::@s4.net_array_element_event::@s6.out, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, reg>>} {
                }
                talos.sv.expression.element_select attributes {is_signed = false, node_id = 16 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "net_array_element_event.n", referenced_symbol = @s1.$root::@s3.net_array_element_event::@s4.net_array_element_event::@s5.n, semantic_type = !talos.ranged_unpacked_array<1 : 0 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: simulation.func private @unit_0(
// CHECK-SAME: %[[NET:[^:]*]]: !simulation.net<!simulation.unpacked_array<1 : 0 x !simulation.packed_array<3 : 0 x !simulation.logic<1>>, #simulation.storage_layout<size = 2, alignment = 1, stride = 1, offsets = []>>>
// CHECK: %[[OBSERVER:.*]] = simulation.observer.bind @observer_
// CHECK-SAME: values(%[[NET]]
// CHECK: simulation.suspend.observe %[[OBSERVER]],

// CHECK: simulation.func private @observer_
// CHECK: %[[ELEMENT:.*]] = simulation.net.extract %{{.*}} from 8
// CHECK: simulation.net.read %[[ELEMENT]]
