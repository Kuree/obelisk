// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.a"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s7.b"} {}
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 8 : i64, sym_name = "s8.cb"} {
          obelisk.sv.symbol.clock_var attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = true, hierarchical_name = "top.cb.out", input_edge = 0 : i32, lifetime = 1 : i32, name = "out", node_id = 11 : i64, output_edge = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s9.out"} {
            obelisk.sv.expression.concatenation attributes {node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.element_select attributes {node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {}
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
              }
              obelisk.sv.expression.element_select attributes {node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {}
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
              }
            }
            obelisk.sv.timing.delay attributes {node_id = 24 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 19 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 22 : i64, referenced_path = "top.cb.out", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s9.out, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {}
              obelisk.sv.expression.integer_literal attributes {constant_value = "2'b10", node_id = 23 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {}
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0.$clocking_output.22.1
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.nba.enqueue {{.*}} {clocking_output = [[GROUP:[0-9]+]] : i64
// CHECK-LABEL: obelisk_sim.func private @unit_0.$clocking_output.22.2
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.nba.enqueue {{.*}} {clocking_output = [[GROUP]] : i64
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-COUNT-2: obelisk_sim.ref.subelement
// CHECK: obelisk_sim.spawn @unit_0.$clocking_output.22.1
// CHECK: obelisk_sim.spawn @unit_0.$clocking_output.22.2
// CHECK-NOT: obelisk.sv.
