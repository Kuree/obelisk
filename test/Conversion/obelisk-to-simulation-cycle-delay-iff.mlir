// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.enable", lifetime = 1 : i32, name = "enable", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.enable"} {}
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 7 : i64, sym_name = "s7.cb"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
            obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 1 : i32, clocking_event_has_iff, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, node_id = 10 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 13 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.enable", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 16 : i64} {}
          }
        }
      }
    }
  }
}

// The two observer closures are bound once. The counter loop reloads the
// primary value and waits for two qualified posedges, resuming in Reactive.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-COUNT-2: simulation.observer.bind
// CHECK: ^[[WAIT:bb[0-9]+]]
// CHECK: simulation.ref.load
// CHECK: simulation.suspend.observe
// CHECK-SAME: conditions 1 edges [1] indices [0]
// CHECK-SAME: resume_region = 10 : i32
// CHECK: arith.subi
// CHECK: cf.cond_br {{.*}}, ^[[WAIT]]
// CHECK-NOT: obelisk.sv.
