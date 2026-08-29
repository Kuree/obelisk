// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Minimal semantic MLIR for IEEE 1800-2017 31.4.2/.3 event mode. Conditions
// require one unqualified shadow subscription, while all state remains static
// scalar actor-local storage and exact clock-occurrence cohorts.

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
          hierarchical_name = "top.condition", lifetime = 1 : i32,
          name = "condition", node_id = 5 : i64, semantic_type = !logic1,
          sym_name = "condition"} {}
      obelisk.sv.symbol.specify_block attributes {
          hierarchical_name = "top", node_id = 6 : i64,
          sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 7 : i64,
            sym_name = "timeskew", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64, time_precision_fs = 1000 : i64,
            timing_check_event_based = true,
            timing_check_remain_active = false,
            timing_check_kind = 8 : i32, timing_check_arg_count = 6 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1, 0, 1, 1>,
            timing_check_arg_has_condition = array<i64: 1, 0, 0, 0, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 2, 3, -1, 4, 5>,
            timing_check_arg_condition_children = array<i64: 1, -1, -1, -1, -1, -1>,
            timing_check_arg_edges = [1 : i32, 1 : i32, 0 : i32, 0 : i32, 0 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], [], [], [], [], []],
            timing_check_arg_effective_edges = array<i32: 1, 1, 0, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 0, 0, 0>,
            timing_check_arg_time_fs = array<i64: 0, 0, 2000000, 0, 0, 0>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 8 : i64, referenced_path = "top.reference",
              referenced_symbol = @root::@body::@reference,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 9 : i64, referenced_path = "top.condition",
              referenced_symbol = @root::@body::@condition,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 10 : i64, referenced_path = "top.data",
              referenced_symbol = @root::@body::@data,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 11 : i64, constant_value = "2",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 12 : i64, constant_value = "1",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 13 : i64, constant_value = "0",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 14 : i64,
            sym_name = "fullskew", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64, time_precision_fs = 1000 : i64,
            timing_check_event_based = true,
            timing_check_remain_active = true,
            timing_check_kind = 9 : i32, timing_check_arg_count = 7 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1, 1, 0, 1, 1>,
            timing_check_arg_has_condition = array<i64: 1, 0, 0, 0, 0, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 2, 3, 4, -1, 5, 6>,
            timing_check_arg_condition_children = array<i64: 1, -1, -1, -1, -1, -1, -1>,
            timing_check_arg_edges = [1 : i32, 1 : i32, 0 : i32, 0 : i32, 0 : i32, 0 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], [], [], [], [], [], []],
            timing_check_arg_effective_edges = array<i32: 1, 1, 0, 0, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1, 0, 0, 0>,
            timing_check_arg_time_fs = array<i64: 0, 0, 2000000, 3000000, 0, 0, 0>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 15 : i64, referenced_path = "top.reference",
              referenced_symbol = @root::@body::@reference,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 16 : i64, referenced_path = "top.condition",
              referenced_symbol = @root::@body::@condition,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 17 : i64, referenced_path = "top.data",
              referenced_symbol = @root::@body::@data,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 18 : i64, constant_value = "2",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 19 : i64, constant_value = "3",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 20 : i64, constant_value = "1",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 21 : i64, constant_value = "1",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 22 : i64,
            sym_name = "timer_timeskew", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64, time_precision_fs = 1000 : i64,
            timing_check_event_based = false,
            timing_check_remain_active = false,
            timing_check_kind = 8 : i32, timing_check_arg_count = 6 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1, 0, 0, 0>,
            timing_check_arg_has_condition = array<i64: 0, 0, 0, 0, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, -1, -1, -1>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1, -1, -1>,
            timing_check_arg_edges = [1 : i32, 1 : i32, 0 : i32, 0 : i32, 0 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], [], [], [], [], []],
            timing_check_arg_effective_edges = array<i32: 1, 1, 0, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 0, 0, 0>,
            timing_check_arg_time_fs = array<i64: 0, 0, 2000000, 0, 0, 0>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 23 : i64, referenced_path = "top.reference",
              referenced_symbol = @root::@body::@reference,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 24 : i64, referenced_path = "top.data",
              referenced_symbol = @root::@body::@data,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 25 : i64, constant_value = "2",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
      }
    }
  }
}

// CHECK: timing_check_kind = 8 : i32
// CHECK: obelisk_sim.suspend.clock_set
// CHECK-SAME: conditions 1 edges [1, 1, 1] indices [0, -1, -1]
// CHECK-SAME: slot_final
// CHECK: arith.cmpi ugt
// The drain-loop backedge carries timestamp, validity, both exact occurrence
// counts, and the ordered violation count; no runtime timing-check object is
// materialized.
// CHECK: arith.addi
// CHECK: cf.br ^{{.*}}({{.*}} : i64, i1, i64, i64, i64)
// CHECK: timing_check_kind = 9 : i32
// CHECK: obelisk_sim.suspend.clock_set
// CHECK-SAME: conditions 1 edges [1, 1, 1] indices [0, -1, -1]
// CHECK-SAME: slot_final
// CHECK: arith.cmpi ugt
// $fullskew additionally carries its last timestamp direction actor-locally.
// CHECK: arith.addi
// CHECK: cf.br ^{{.*}}({{.*}} : i64, i1, i64, i64, i64, i1)
// Timer mode inventories one private scalar, one event, and one statically
// spawned helper.  The coordinator directly replaces/cancels the single
// delayed maturity; the helper wakes only when that maturity becomes current.
// There is no dynamic spawn, generic timing op, or runtime timing table.
// CHECK: obelisk_sim.storage.decl 3
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: timing_check_event_based = false
// CHECK: obelisk_sim.event.create
// CHECK-COUNT-1: obelisk_sim.spawn @unit_2.$timing_timer
// Deadline addition saturates rather than wrapping to an early expiry.
// CHECK: arith.cmpi ult
// CHECK: arith.select
// CHECK: obelisk_sim.event.trigger {{.*}} after {{.*}} nonblocking = true {replaceable
// Cancellation uses the same compiler-private event without a delayed stale
// entry.  No ordinary blocking trigger is introduced.
// CHECK: obelisk_sim.event.trigger {{.*}} nonblocking = true {replaceable
// CHECK-LABEL: obelisk_sim.func private @unit_2.$timing_timer
// CHECK: obelisk_sim.suspend.event
// CHECK: obelisk_sim.ref.store
// CHECK-NOT: obelisk_sim.event.trigger
// CHECK-NOT: obelisk_sim.spawn
// CHECK-NOT: timing_check_table
// CHECK-NOT: timing_check_timer
// CHECK-NOT: sdf
