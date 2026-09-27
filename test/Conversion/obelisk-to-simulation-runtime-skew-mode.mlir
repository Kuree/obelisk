// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

// Semantic-only coverage for the bounded invariant fallback of IEEE
// 1800-2017 31.4.2.  Driver source admission still requires constant flags;
// an absent BoolAttr here asks lowering to evaluate the existing flag child.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
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
          hierarchical_name = "top.event_mode", lifetime = 1 : i32,
          name = "event_mode", node_id = 5 : i64, semantic_type = !logic1,
          sym_name = "event_mode"} {}
      obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.remain_active", lifetime = 1 : i32,
          name = "remain_active", node_id = 6 : i64, semantic_type = !logic1,
          sym_name = "remain_active"} {}
      obelisk.sv.symbol.specify_block attributes {
          hierarchical_name = "top", node_id = 7 : i64,
          sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 8 : i64,
            sym_name = "runtime_timeskew", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64, time_precision_fs = 1000 : i64,
            timing_check_kind = 8 : i32, timing_check_arg_count = 6 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1, 0, 1, 1>,
            timing_check_arg_has_condition = array<i64: 0, 0, 0, 0, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, -1, 3, 4>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1, -1, -1>,
            timing_check_arg_edges = [1 : i32, 1 : i32, 0 : i32, 0 : i32, 0 : i32, 0 : i32],
            timing_check_arg_edge_descriptors = [[], [], [], [], [], []],
            timing_check_arg_effective_edges = array<i32: 1, 1, 0, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 0, 0, 0>,
            timing_check_arg_time_fs = array<i64: 0, 0, 2000000, 0, 0, 0>} {
          obelisk.sv.expression.named_value attributes {
              node_id = 9 : i64, referenced_path = "top.reference",
              referenced_symbol = @root::@body::@reference,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 10 : i64, referenced_path = "top.data",
              referenced_symbol = @root::@body::@data,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 11 : i64, constant_value = "2",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 12 : i64, referenced_path = "top.event_mode",
              referenced_symbol = @root::@body::@event_mode,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 13 : i64, referenced_path = "top.remain_active",
              referenced_symbol = @root::@body::@remain_active,
              semantic_type = !logic1} {}
        }
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 14 : i64,
            sym_name = "runtime_fullskew", obelisk.basic_timing_check,
            time_unit_fs = 1000000 : i64, time_precision_fs = 1000 : i64,
            timing_check_kind = 9 : i32, timing_check_arg_count = 7 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1, 1, 0, 1, 1>,
            timing_check_arg_has_condition = array<i64: 0, 0, 0, 0, 0, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3, -1, 4, 5>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1, -1, -1, -1>,
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
              node_id = 16 : i64, referenced_path = "top.data",
              referenced_symbol = @root::@body::@data,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 17 : i64, constant_value = "2",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {
              node_id = 18 : i64, constant_value = "3",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 19 : i64, referenced_path = "top.event_mode",
              referenced_symbol = @root::@body::@event_mode,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 20 : i64, referenced_path = "top.remain_active",
              referenced_symbol = @root::@body::@remain_active,
              semantic_type = !logic1} {}
        }
      }
    }
  }
}

// Each timer-capable check receives exactly one private helper inventory.
// CHECK-COUNT-1: simulation.storage.decl 4
// CHECK-LABEL: simulation.func private @unit_0(
// The two flag handles are loaded and converted before the first suspension,
// exactly once each.  No mode-dependent re-evaluation enters the wait loop.
// CHECK: %[[EVENT_LOGIC:.*]] = simulation.ref.load
// CHECK: %[[EVENT_MODE:.*]] = simulation.logic.is_true %[[EVENT_LOGIC]]
// CHECK: %[[REMAIN_LOGIC:.*]] = simulation.ref.load
// CHECK: %[[REMAIN:.*]] = simulation.logic.is_true %[[REMAIN_LOGIC]]
// CHECK-NOT: simulation.logic.is_true
// CHECK: simulation.event.create
// CHECK-COUNT-1: simulation.spawn @unit_0.$timing_timer
// CHECK: simulation.suspend.clock_set
// Both established cohort state machines exist only for the unknown mode and
// are selected by the invariant entry value at process and slot finalization.
// CHECK: cf.cond_br %[[EVENT_MODE]], ^[[EVENT_PROCESS:bb[0-9]+]], ^[[TIMER_PROCESS:bb[0-9]+]]
// CHECK: ^[[EVENT_PROCESS]]:
// CHECK: arith.cmpi ugt
// A deferred remain_active value gates event-mode violation eligibility,
// multiplicity, and deactivation instead of being resampled.
// CHECK: arith.ori %{{.*}}, %[[REMAIN]] : i1
// CHECK: arith.select %[[REMAIN]],
// CHECK: arith.xori %[[REMAIN]],
// CHECK: cf.br ^[[PROCESS_JOIN:bb[0-9]+]]
// CHECK: ^[[TIMER_PROCESS]]:
// The same invariant gates timer-mode false-reference dormancy.
// CHECK: arith.xori %[[REMAIN]],
// CHECK: arith.cmpi ule
// CHECK: cf.br ^[[PROCESS_JOIN]]
// CHECK: cf.cond_br %[[EVENT_MODE]], ^[[EVENT_FINAL:bb[0-9]+]], ^[[TIMER_FINAL:bb[0-9]+]]
// CHECK: ^[[EVENT_FINAL]]:
// CHECK: cf.br ^[[MODE_JOIN:bb[0-9]+]](
// CHECK: ^[[TIMER_FINAL]]:
// CHECK: simulation.event.trigger
// CHECK: cf.br ^[[MODE_JOIN]](
// Fullskew shares the bounded selector contract while retaining its existing
// directional state machines and a distinct serial timer helper.
// CHECK-COUNT-1: simulation.storage.decl 5
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-COUNT-2: simulation.logic.is_true
// CHECK-COUNT-1: simulation.event.create
// CHECK-COUNT-1: simulation.spawn @unit_1.$timing_timer
// CHECK: cf.cond_br
// CHECK: arith.cmpi ugt
// CHECK: arith.cmpi ule
// CHECK: cf.cond_br
// CHECK-LABEL: simulation.func private @unit_0.$timing_timer
// CHECK: simulation.suspend.event
// CHECK-LABEL: simulation.func private @unit_1.$timing_timer
// CHECK: simulation.suspend.event
// CHECK-NOT: timing_check_table
// CHECK-NOT: timing_check_timer
