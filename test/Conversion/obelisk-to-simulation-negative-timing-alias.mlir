// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --implicit-check-not='hierarchy "top.y"'
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// IEEE 1800-2017 31.9.1 creates one implicit delayed signal per physical
// terminal, not per spelling.  Whole-terminal aliases x and y therefore share
// one delayed monitor across both timing checks.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
      sym_name = "root"} {
    obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 2 : i64,
        sym_name = "body", time_unit_fs = 1000 : i64,
        time_precision_fs = 1000 : i64} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.x",
          lifetime = 1 : i32, name = "x", node_id = 3 : i64,
          semantic_type = !logic1, sym_name = "x"} {}
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.y",
          lifetime = 1 : i32, name = "y", node_id = 4 : i64,
          semantic_type = !logic1, sym_name = "y"} {}
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.z",
          lifetime = 1 : i32, name = "z", node_id = 5 : i64,
          semantic_type = !logic1, sym_name = "z"} {}
      obelisk.sv.symbol.net_alias attributes {
          hierarchical_name = "top", node_id = 6 : i64, sym_name = "xy"} {
        obelisk.sv.expression.named_value attributes {node_id = 7 : i64,
            referenced_path = "top.x", referenced_symbol = @root::@body::@x,
            semantic_type = !logic1} {}
        obelisk.sv.expression.named_value attributes {node_id = 8 : i64,
            referenced_path = "top.y", referenced_symbol = @root::@body::@y,
            semantic_type = !logic1} {}
      }
      obelisk.sv.symbol.specify_block attributes {
          hierarchical_name = "top", node_id = 9 : i64,
          sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 10 : i64,
            sym_name = "via_x", obelisk.basic_timing_check,
            obelisk.negative_timing_check, time_unit_fs = 1000 : i64,
            time_precision_fs = 1000 : i64, timing_check_kind = 3 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>,
            timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, -2000, 5000>} {
          obelisk.sv.expression.named_value attributes {node_id = 11 : i64,
              referenced_path = "top.x", referenced_symbol = @root::@body::@x,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 12 : i64,
              referenced_path = "top.z", referenced_symbol = @root::@body::@z,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 13 : i64,
              constant_value = "-2", semantic_type = !int} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 14 : i64,
              constant_value = "5", semantic_type = !int} {}
        }
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 20 : i64,
            sym_name = "via_y", obelisk.basic_timing_check,
            obelisk.negative_timing_check, time_unit_fs = 1000 : i64,
            time_precision_fs = 1000 : i64, timing_check_kind = 3 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>,
            timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, -3000, 6000>} {
          obelisk.sv.expression.named_value attributes {node_id = 21 : i64,
              referenced_path = "top.y", referenced_symbol = @root::@body::@y,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 22 : i64,
              referenced_path = "top.z", referenced_symbol = @root::@body::@z,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 23 : i64,
              constant_value = "-3", semantic_type = !int} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 24 : i64,
              constant_value = "6", semantic_type = !int} {}
        }
      }
    }
  }
}

// The zero-delay physical terminal is represented by the -1 direct-source
// sentinel and needs no redundant storage/monitor.
// CHECK-COUNT-1: debug "implicit negative timing-check delayed signal"
// CHECK-COUNT-2: simulation.timing_delayed_storage_ids = array<i64: 2, -1>
// CHECK-COUNT-1: simulation.negative_timing_delay_monitor
// CHECK-NOT: timing_check_table
