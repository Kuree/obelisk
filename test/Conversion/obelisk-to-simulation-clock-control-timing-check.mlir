// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Minimal semantic MLIR coverage for the Clause 31.4 event-distance actors.
// Every check lowers to the existing exact occurrence queue and static scalar
// comparisons; $width derives its opposing event without another semantic
// expression or a generic timing-check operation.

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
          hierarchical_name = "top.period", lifetime = 1 : i32,
          name = "period", node_id = 5 : i64, semantic_type = !logic1,
          sym_name = "period"} {}
      obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.width", lifetime = 1 : i32,
          name = "width", node_id = 6 : i64, semantic_type = !logic1,
          sym_name = "width"} {}
      obelisk.sv.symbol.specify_block attributes {
          hierarchical_name = "top", node_id = 7 : i64,
          sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 8 : i64,
            sym_name = "skew", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64,
            time_precision_fs = 1000 : i64,
            timing_check_kind = 7 : i32,
            timing_check_arg_count = 3 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1>,
            timing_check_arg_has_condition = array<i64: 0, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 1, 2>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1>,
            timing_check_arg_edges = [1 : i32, 1 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], [], []],
            timing_check_arg_effective_edges = array<i32: 1, 1, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, 3000000>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 9 : i64, referenced_path = "top.reference",
              referenced_symbol = @root::@body::@reference,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 10 : i64, referenced_path = "top.data",
              referenced_symbol = @root::@body::@data,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 11 : i64, constant_value = "3",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 12 : i64,
            sym_name = "period_check", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64,
            time_precision_fs = 1000 : i64,
            timing_check_kind = 10 : i32,
            timing_check_arg_count = 2 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1>,
            timing_check_arg_has_condition = array<i64: 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 1>,
            timing_check_arg_condition_children = array<i64: -1, -1>,
            timing_check_arg_edges = [1 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], []],
            timing_check_arg_effective_edges = array<i32: 1, 0>,
            timing_check_arg_is_time = array<i64: 0, 1>,
            timing_check_arg_time_fs = array<i64: 0, 4000000>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 13 : i64, referenced_path = "top.period",
              referenced_symbol = @root::@body::@period,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 14 : i64, constant_value = "4",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 15 : i64,
            sym_name = "width_check", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64,
            time_precision_fs = 1000 : i64,
            timing_check_kind = 11 : i32,
            timing_check_arg_count = 2 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1>,
            timing_check_arg_has_condition = array<i64: 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 1>,
            timing_check_arg_condition_children = array<i64: -1, -1>,
            timing_check_arg_edges = [1 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], []],
            timing_check_arg_effective_edges = array<i32: 1, 0>,
            timing_check_arg_is_time = array<i64: 0, 1>,
            timing_check_arg_time_fs = array<i64: 0, 5000000>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 16 : i64, referenced_path = "top.width",
              referenced_symbol = @root::@body::@width,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 17 : i64, constant_value = "5",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
      }
    }
  }
}

// CHECK: home_region = 8 : i32
// CHECK-SAME: timing_check_kind = 7 : i32
// CHECK: simulation.suspend.clock_set
// CHECK-SAME: edges [1, 1]
// CHECK-SAME: resume_region = 8 : i32
// CHECK-SAME: slot_final
// CHECK: arith.addi
// CHECK: arith.cmpi ugt
// CHECK: timing_check_kind = 10 : i32
// CHECK-NOT: slot_final
// CHECK: simulation.suspend.clock_set
// CHECK-SAME: edges [1]
// CHECK: arith.cmpi ult
// CHECK: timing_check_kind = 11 : i32
// CHECK-NOT: slot_final
// CHECK: simulation.suspend.clock_set
// CHECK-SAME: edges [1, 2]
// CHECK: arith.cmpi ugt
// CHECK: arith.cmpi ult
// CHECK-NOT: timing_check_table
