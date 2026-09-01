// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

!logic = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk.sv.symbol.definition attributes {
      definition_kind = 0 : i32, hierarchical_name = "path_extra_input",
      name = "path_extra_input", node_id = 0 : i64,
      sym_name = "s0.path_extra_input"} {}
  obelisk.sv.symbol.root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
      sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {
        hierarchical_name = "path_extra_input", is_uninstantiated = false,
        name = "path_extra_input", node_id = 3 : i64,
        referenced_path = "path_extra_input",
        referenced_symbol = @s0.path_extra_input,
        sym_name = "s3.path_extra_input"} {
      obelisk.sv.symbol.instance_body attributes {
          hierarchical_name = "path_extra_input", name = "path_extra_input",
          node_id = 4 : i64, sym_name = "s4.path_extra_input",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net attributes {
            hierarchical_name = "path_extra_input.source", is_implicit = false,
            name = "source",
            net_kind = 1 : i32, node_id = 5 : i64,
            semantic_type = !logic, sym_name = "s5.source"} {}
        obelisk.sv.symbol.net attributes {
            hierarchical_name = "path_extra_input.control", is_implicit = false,
            name = "control",
            net_kind = 1 : i32, node_id = 6 : i64,
            semantic_type = !logic, sym_name = "s6.control"} {}
        obelisk.sv.symbol.net attributes {
            hierarchical_name = "path_extra_input.output", is_implicit = false,
            name = "output",
            net_kind = 1 : i32, node_id = 7 : i64,
            semantic_type = !logic, sym_name = "s7.output"} {}
        obelisk.sv.symbol.continuous_assign attributes {
            hierarchical_name = "path_extra_input", node_id = 8 : i64,
            sym_name = "s8", time_precision_fs = 1000000 : i64,
            time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, node_id = 9 : i64,
              semantic_type = !logic} {
            obelisk.sv.expression.named_value attributes {
                node_id = 10 : i64,
                referenced_path = "path_extra_input.output",
                referenced_symbol = @s1.$root::@s3.path_extra_input::@s4.path_extra_input::@s7.output,
                semantic_type = !logic} {}
            obelisk.sv.expression.binary_op attributes {
                node_id = 11 : i64, operator_kind = 19 : i32,
                semantic_type = !logic} {
              obelisk.sv.expression.named_value attributes {
                  node_id = 12 : i64,
                  referenced_path = "path_extra_input.source",
                  referenced_symbol = @s1.$root::@s3.path_extra_input::@s4.path_extra_input::@s5.source,
                  semantic_type = !logic} {}
              obelisk.sv.expression.named_value attributes {
                  node_id = 13 : i64,
                  referenced_path = "path_extra_input.control",
                  referenced_symbol = @s1.$root::@s3.path_extra_input::@s4.path_extra_input::@s6.control,
                  semantic_type = !logic} {}
            }
          }
        }
        obelisk.sv.symbol.specify_block attributes {
            hierarchical_name = "path_extra_input", node_id = 14 : i64,
            sym_name = "s9"} {
          obelisk.sv.symbol.timing_path attributes {
              hierarchical_name = "path_extra_input", node_id = 15 : i64,
              obelisk.simple_timing_path, sym_name = "s10",
              timing_connection_full = false,
              timing_delay_fs = array<i64: 2000000>,
              timing_input_terminals = [{low = 0 : i64,
                path = "path_extra_input.source", root_width = 1 : i64,
                width = 1 : i64}],
              timing_output_terminal = {low = 0 : i64,
                path = "path_extra_input.output", root_width = 1 : i64,
                width = 1 : i64},
              timing_polarity = 0 : i32} {}
        }
      }
    }
  }
}

// A control-only activation has no applicable module path and therefore uses
// the masked runtime's immediate outside-path update. Source activations retain
// the declared two-tick path delay.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[ACTIVE:.+]] = obelisk_sim.logic.case_difference_mask
// CHECK: %[[TICKS:.+]] = arith.constant {{.*}} 2 : i64
// CHECK: %[[DELAY:.+]] = obelisk_sim.time.scale %[[TICKS]]
// CHECK: obelisk_sim.driver.drive_inertial_path
// CHECK-SAME: active %[[ACTIVE]]
// CHECK-SAME: after[%[[DELAY]], %[[DELAY]], %[[DELAY]]]
// CHECK: obelisk_sim.suspend.any
// CHECK-NOT: obelisk.sv.
