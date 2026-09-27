// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

!logic2 = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!logic4 = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!int = !obelisk.integral<32, true, false, 31 : 0, int>

module {
  obelisk.sv.symbol.definition attributes {
      definition_kind = 0 : i32, hierarchical_name = "split_path",
      name = "split_path", node_id = 0 : i64, sym_name = "s0.split_path"} {}
  obelisk.sv.symbol.root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
      sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {
        hierarchical_name = "split_path", is_uninstantiated = false,
        name = "split_path", node_id = 3 : i64,
        referenced_path = "split_path",
        referenced_symbol = @s0.split_path, sym_name = "s3.split_path"} {
      obelisk.sv.symbol.instance_body attributes {
          hierarchical_name = "split_path", name = "split_path",
          node_id = 4 : i64, sym_name = "s4.split_path",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net attributes {
            hierarchical_name = "split_path.source", is_implicit = false,
            name = "source", net_kind = 1 : i32, node_id = 5 : i64,
            semantic_type = !logic4, sym_name = "s5.source"} {}
        obelisk.sv.symbol.net attributes {
            hierarchical_name = "split_path.output", is_implicit = false,
            name = "output", net_kind = 1 : i32, node_id = 6 : i64,
            semantic_type = !logic4, sym_name = "s6.output"} {}

        obelisk.sv.symbol.continuous_assign attributes {
            hierarchical_name = "split_path", node_id = 7 : i64,
            sym_name = "s7", time_precision_fs = 1000000 : i64,
            time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, node_id = 8 : i64,
              semantic_type = !logic4} {
            obelisk.sv.expression.concatenation attributes {
                node_id = 9 : i64, semantic_type = !logic4} {
              obelisk.sv.expression.range_select attributes {
                  node_id = 10 : i64, selection_kind = 0 : i32,
                  semantic_type = !logic2} {
                obelisk.sv.expression.named_value attributes {
                    node_id = 11 : i64,
                    referenced_path = "split_path.output",
                    referenced_symbol = @s1.$root::@s3.split_path::@s4.split_path::@s6.output,
                    semantic_type = !logic4} {}
                obelisk.sv.expression.integer_literal attributes {
                    constant_value = "3", node_id = 12 : i64,
                    semantic_type = !int} {}
                obelisk.sv.expression.integer_literal attributes {
                    constant_value = "2", node_id = 13 : i64,
                    semantic_type = !int} {}
              }
              obelisk.sv.expression.range_select attributes {
                  node_id = 14 : i64, selection_kind = 0 : i32,
                  semantic_type = !logic2} {
                obelisk.sv.expression.named_value attributes {
                    node_id = 15 : i64,
                    referenced_path = "split_path.output",
                    referenced_symbol = @s1.$root::@s3.split_path::@s4.split_path::@s6.output,
                    semantic_type = !logic4} {}
                obelisk.sv.expression.integer_literal attributes {
                    constant_value = "1", node_id = 16 : i64,
                    semantic_type = !int} {}
                obelisk.sv.expression.integer_literal attributes {
                    constant_value = "0", node_id = 17 : i64,
                    semantic_type = !int} {}
              }
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 18 : i64, referenced_path = "split_path.source",
                referenced_symbol = @s1.$root::@s3.split_path::@s4.split_path::@s5.source,
                semantic_type = !logic4} {}
          }
        }

        obelisk.sv.symbol.specify_block attributes {
            hierarchical_name = "split_path", node_id = 27 : i64,
            sym_name = "s9"} {
          obelisk.sv.symbol.timing_path attributes {
              hierarchical_name = "split_path", node_id = 28 : i64,
              obelisk.simple_timing_path, sym_name = "s10",
              timing_connection_full = false,
              timing_delay_fs = array<i64: 1000000, 2000000, 3000000,
                  4000000, 5000000, 6000000, 7000000, 8000000,
                  9000000, 10000000, 11000000, 12000000>,
              timing_input_terminals = [{low = 0 : i64,
                path = "split_path.source", root_width = 4 : i64,
                width = 4 : i64}],
              timing_output_terminal = {low = 0 : i64,
                path = "split_path.output", root_width = 4 : i64,
                width = 4 : i64},
              timing_polarity = 0 : i32} {}
        }
      }
    }
  }
}

// The one four-bit parallel path is clipped into two two-bit driver-local
// plans. Its upper owner reads source[3:2], while its lower owner reads [1:0].
// CHECK-COUNT-2: simulation.storage.decl
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: driver_node_id = 15 : i64{{.*}}input_lows = array<i64: 0>{{.*}}output_root_width = 2 : i64{{.*}}driver_node_id = 11 : i64{{.*}}input_lows = array<i64: 2>{{.*}}output_root_width = 2 : i64
// CHECK-COUNT-2: simulation.logic.case_difference_mask
// CHECK: simulation.driver.read
// CHECK-COUNT-12: simulation.driver.drive_inertial_path
// CHECK: simulation.driver.read
// CHECK-COUNT-12: simulation.driver.drive_inertial_path
