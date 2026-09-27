// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Clause 31.4.6 needs only one specialized state update in otherwise ordinary
// exact clock-cohort MLIR. Signed static offsets survive Clause 3.14.1 freezing
// and the opposite trailing edge is derived without a third semantic event.

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
      obelisk.sv.symbol.specify_block attributes {
          hierarchical_name = "top", node_id = 5 : i64,
          sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 6 : i64,
            sym_name = "nochange", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64,
            time_precision_fs = 1000 : i64,
            timing_check_kind = 12 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1, 1>,
            timing_check_arg_has_condition = array<i64: 0, 0, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>,
            timing_check_arg_edges = [1 : i32, 0 : i32, 0 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], [], [], []],
            timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, -3000000, 2000000>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 7 : i64, referenced_path = "top.reference",
              referenced_symbol = @root::@body::@reference,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 8 : i64, referenced_path = "top.data",
              referenced_symbol = @root::@body::@data,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 9 : i64, constant_value = "-3",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 10 : i64, constant_value = "2",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
      }
    }
  }
}

// CHECK: home_region = 8 : i32
// CHECK-SAME: timing_check_kind = 12 : i32
// CHECK: arith.constant -3000 : i64
// CHECK: arith.constant 2000 : i64
// CHECK: simulation.suspend.clock_set
// CHECK-SAME: edges [1, 0, 2]
// CHECK-SAME: resume_region = 8 : i32
// CHECK-SAME: slot_final
// CHECK: %[[MASK:.+]] = simulation.assert.clock_occurrence.consume
// CHECK: simulation.assert.nochange.update {{.*}}, %[[MASK]] offsets(%{{.+}}, %{{.+}})
// CHECK: simulation.assert.nochange.update {{.*}}, %{{.+}} offsets(%{{.+}}, %{{.+}})
// CHECK-NOT: timing_check_table
