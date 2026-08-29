// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Minimal semantic MLIR coverage for the positive Clause 31.3.3 combined
// actor. Both directions share one exact occurrence cohort and one violation
// branch; there is no generic timing-check operation or runtime table.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk.sv.symbol.root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
      sym_name = "root"} {
    obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 2 : i64,
        sym_name = "body", time_unit_fs = 1000000 : i64,
        time_precision_fs = 1000 : i64} {
      obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.reference", lifetime = 1 : i32,
          name = "reference", node_id = 3 : i64, semantic_type = !logic1,
          sym_name = "reference"} {}
      obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.data", lifetime = 1 : i32,
          name = "data", node_id = 4 : i64, semantic_type = !logic1,
          sym_name = "data"} {}
      obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.reference_condition", lifetime = 1 : i32,
          name = "reference_condition", node_id = 11 : i64,
          semantic_type = !logic1, sym_name = "reference_condition"} {}
      obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.data_condition", lifetime = 1 : i32,
          name = "data_condition", node_id = 12 : i64,
          semantic_type = !logic1, sym_name = "data_condition"} {}
      obelisk.sv.symbol.specify_block attributes {
          hierarchical_name = "top", node_id = 5 : i64,
          sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 6 : i64,
            sym_name = "check", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64,
            time_precision_fs = 1000 : i64,
            timing_check_kind = 3 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1, 1>,
            timing_check_arg_has_condition = array<i64: 1, 1, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 2, 4, 5>,
            timing_check_arg_condition_children = array<i64: 1, 3, -1, -1>,
            timing_check_arg_edges = [1 : i32, 1 : i32, 0 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], [], [], []],
            timing_check_arg_effective_edges = array<i32: 1, 1, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, 3000000, 5000000>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 7 : i64, referenced_path = "top.reference",
              referenced_symbol = @root::@body::@reference,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 13 : i64, referenced_path = "top.reference_condition",
              referenced_symbol = @root::@body::@reference_condition,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 8 : i64, referenced_path = "top.data",
              referenced_symbol = @root::@body::@data,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 14 : i64, referenced_path = "top.data_condition",
              referenced_symbol = @root::@body::@data_condition,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 9 : i64, constant_value = "3",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 10 : i64, constant_value = "5",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
      }
    }
  }
}

// Two timestamp/valid pairs are carried as ordinary SSA continuation state.
// CHECK: obelisk_sim.func private
// CHECK-SAME: obelisk_sim.timing_check_coordinator
// CHECK: cf.br ^{{.*}}({{.*}} : i64, i1, i64, i1)
// CHECK: obelisk_sim.suspend.clock_set
// CHECK-SAME: conditions 2 edges [1, 1] indices [0, 1]
// CHECK: obelisk_sim.assert.clock_occurrence.consume
// CHECK-COUNT-2: arith.cmpi ult
// CHECK: arith.ori
// CHECK-NOT: timing_check_table
