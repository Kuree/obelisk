// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 31.7 forms a scalar equality from the packed conditioning
// expression at its event, while 31.5 freezes custom transition classes and
// 31.8 treats a whole vector publication as one timing-check event. Keep all
// three policies on the existing exact clock-set wait.

!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>

module {
  obelisk.sv.symbol.root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
      sym_name = "root"} {
    obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 2 : i64,
        sym_name = "body", time_unit_fs = 1000000 : i64,
        time_precision_fs = 1000 : i64} {
      obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.data", lifetime = 1 : i32,
          name = "data", node_id = 3 : i64, semantic_type = !logic4,
          sym_name = "data"} {}
      obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.reference", lifetime = 1 : i32,
          name = "reference", node_id = 4 : i64, semantic_type = !logic4,
          sym_name = "reference"} {}
      obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.condition", lifetime = 1 : i32,
          name = "condition", node_id = 5 : i64, semantic_type = !logic4,
          sym_name = "condition"} {}
      obelisk.sv.symbol.specify_block attributes {
          hierarchical_name = "top", node_id = 6 : i64,
          sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 7 : i64,
            sym_name = "check", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64,
            time_precision_fs = 1000 : i64,
            timing_check_kind = 1 : i32,
            timing_check_arg_count = 3 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1>,
            timing_check_arg_has_condition = array<i64: 1, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 2, 3>,
            timing_check_arg_condition_children = array<i64: 1, -1, -1>,
            timing_check_arg_condition_predicates = array<i32: 2, -1, -1>,
            timing_check_arg_edges = [0 : i32, 3 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], [], []],
            timing_check_arg_effective_edges = array<i32: 258, 3, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, 3000000>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 8 : i64, referenced_path = "top.data",
              referenced_symbol = @root::@body::@data,
              semantic_type = !logic4} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 9 : i64, referenced_path = "top.condition",
              referenced_symbol = @root::@body::@condition,
              semantic_type = !logic4} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 10 : i64, referenced_path = "top.reference",
              referenced_symbol = @root::@body::@reference,
              semantic_type = !logic4} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 11 : i64, constant_value = "3",
              semantic_type = !int} {}
        }
      }
    }
  }
}

// CHECK: obelisk_sim.func private
// CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.logic<4>>
// CHECK-SAME: obelisk_sim.timing_check_coordinator
// CHECK-COUNT-1: obelisk_sim.suspend.clock_set
// CHECK-SAME: conditions 1 edges [258, 3] indices [0, -1]
// CHECK-SAME: condition_predicates = array<i32: 2>
// CHECK-NOT: obelisk_sim.ref.extract
// CHECK-NOT: obelisk_sim.suspend.observe
// CHECK-NOT: timing_check_table
