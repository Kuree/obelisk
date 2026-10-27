// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

module {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
        talos.sv.symbol.net @s6.q attributes {hierarchical_name = "top.q", is_implicit = false, name = "q", net_kind = 1 : i32, node_id = 6 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {}
        talos.sv.symbol.variable @s10.index attributes {hierarchical_name = "top.index", lifetime = 1 : i32, name = "index", node_id = 17 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
        talos.sv.symbol.clocking_block @s7.cb attributes {hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 7 : i64} {
          talos.sv.symbol.clock_var @s8.q attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.q", input_edge = 0 : i32, lifetime = 1 : i32, name = "q", node_id = 8 : i64, output_edge = 0 : i32, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {}
        }
        talos.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 11 : i64, semantic_type = !talos.ranged_packed_array<2 : 1 x !talos.integral<1, false, true, 0 : 0, logic>>} {
              talos.sv.expression.range_select attributes {node_id = 14 : i64, selection_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<2 : 1 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                talos.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 12 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.cb::@s8.q, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {}
                talos.sv.expression.integer_literal attributes {constant_value = "2", is_signed = true, node_id = 15 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
                talos.sv.expression.integer_literal attributes {constant_value = "1", is_signed = true, node_id = 16 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
              }
              talos.sv.expression.integer_literal attributes {constant_value = "2'b11", node_id = 13 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {}
            }
          }
          talos.sv.statement.expression_statement attributes {node_id = 18 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 19 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              talos.sv.expression.element_select attributes {node_id = 20 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                talos.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 21 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.cb::@s8.q, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {}
                talos.sv.expression.named_value attributes {is_signed = true, node_id = 22 : i64, referenced_path = "top.index", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.index, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
              }
              talos.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 23 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !simulation.packed_array<3 : 0 x !simulation.logic<1>> design hierarchy "top.q" debug "clocking output"
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.12
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.nba.enqueue {{.*}} to {{.*}} : (!simulation.packed_array<2 : 1 x !simulation.logic<1>>, !simulation.driver<!simulation.packed_array<2 : 1 x !simulation.logic<1>>>) -> ()
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.21
// CHECK: simulation.nba.enqueue {{.*}} to {{.*}} : (!simulation.logic<1>, !simulation.driver<!simulation.logic<1>>) -> ()
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[SELECTED:.*]] = simulation.driver.extract %[[DRIVER:arg[0-9]+]] from 1
// CHECK: simulation.spawn @unit_0.$clocking_output.12
// CHECK-SAME: %[[SELECTED]]
// CHECK: %[[DYNAMIC:.*]] = simulation.driver.array_element %[[DRIVER]]
// CHECK: simulation.spawn @unit_0.$clocking_output.21
// CHECK-SAME: %[[DYNAMIC]]
// CHECK-NOT: talos.sv.
