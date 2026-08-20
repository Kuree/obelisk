// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.17: an event variable declared without an initial value is
// initialized to a new synchronization object. A scalar event owns an event
// descriptor, but the elements of an event array live in ordinary storage,
// whose slots would otherwise all start as the same null handle. The root
// initializer gives each element its own object before any process starts.

// CHECK-LABEL: obelisk_sim.func @__obelisk_root
// CHECK: %[[STORAGE:.*]] = obelisk_sim.context.storage %{{.*}}[0]
// CHECK: %[[FIRST:.*]] = obelisk_sim.ref.subelement %[[STORAGE]]{{\[\[0\]\]}}
// CHECK: %[[FIRSTEVENT:.*]] = obelisk_sim.event.create
// CHECK: obelisk_sim.ref.store %[[FIRSTEVENT]] to %[[FIRST]]
// CHECK: %[[SECOND:.*]] = obelisk_sim.ref.subelement %[[STORAGE]]{{\[\[1\]\]}}
// CHECK: %[[SECONDEVENT:.*]] = obelisk_sim.event.create
// CHECK: obelisk_sim.ref.store %[[SECONDEVENT]] to %[[SECOND]]
// CHECK: obelisk_sim.spawn

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.e", lifetime = 1 : i32, name = "e", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.event>, sym_name = "s5.e"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.event_trigger attributes {node_id = 7 : i64} {
            obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.event} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "t.e", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.e, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.event>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}
