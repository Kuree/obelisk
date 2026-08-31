// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REPAIR
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=ZERO

// IEEE 1800-2017 31.9.1 requires deterministic repair of an inconsistent
// delayed-terminal component.  The independent -5 check must not be selected
// while repairing the mutually inconsistent pair containing two equal -2
// candidates.  Their source order is the stable final tie-breaker: `left` is
// repaired and `right` retains its negative limit.

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
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r",
          lifetime = 1 : i32, name = "r", node_id = 3 : i64,
          semantic_type = !logic1, sym_name = "r"} {}
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d",
          lifetime = 1 : i32, name = "d", node_id = 4 : i64,
          semantic_type = !logic1, sym_name = "d"} {}
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.ir",
          lifetime = 1 : i32, name = "ir", node_id = 5 : i64,
          semantic_type = !logic1, sym_name = "ir"} {}
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.id",
          lifetime = 1 : i32, name = "id", node_id = 6 : i64,
          semantic_type = !logic1, sym_name = "id"} {}
      obelisk.sv.symbol.specify_block attributes {
          hierarchical_name = "top", node_id = 7 : i64,
          sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 10 : i64,
            sym_name = "left", obelisk.basic_timing_check,
            obelisk.negative_timing_check, time_unit_fs = 1000 : i64,
            time_precision_fs = 1000 : i64, timing_check_kind = 3 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>,
            timing_check_arg_effective_edges = array<i32: 1, 1, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, -2000, 5000>} {
          obelisk.sv.expression.named_value attributes {node_id = 11 : i64,
              referenced_path = "top.r", referenced_symbol = @root::@body::@r,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 12 : i64,
              referenced_path = "top.d", referenced_symbol = @root::@body::@d,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 13 : i64,
              constant_value = "-2", semantic_type = !int} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 14 : i64,
              constant_value = "5", semantic_type = !int} {}
        }
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 20 : i64,
            sym_name = "right", obelisk.basic_timing_check,
            obelisk.negative_timing_check, time_unit_fs = 1000 : i64,
            time_precision_fs = 1000 : i64, timing_check_kind = 3 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>,
            timing_check_arg_effective_edges = array<i32: 1, 1, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, 5000, -2000>} {
          obelisk.sv.expression.named_value attributes {node_id = 21 : i64,
              referenced_path = "top.r", referenced_symbol = @root::@body::@r,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 22 : i64,
              referenced_path = "top.d", referenced_symbol = @root::@body::@d,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 23 : i64,
              constant_value = "5", semantic_type = !int} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 24 : i64,
              constant_value = "-2", semantic_type = !int} {}
        }
        obelisk.sv.symbol.system_timing_check attributes {
            hierarchical_name = "top", node_id = 30 : i64,
            sym_name = "independent", obelisk.basic_timing_check,
            obelisk.negative_timing_check, time_unit_fs = 1000 : i64,
            time_precision_fs = 1000 : i64, timing_check_kind = 3 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>,
            timing_check_arg_effective_edges = array<i32: 1, 1, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, -5000, 7000>} {
          obelisk.sv.expression.named_value attributes {node_id = 31 : i64,
              referenced_path = "top.ir", referenced_symbol = @root::@body::@ir,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 32 : i64,
              referenced_path = "top.id", referenced_symbol = @root::@body::@id,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 33 : i64,
              constant_value = "-5", semantic_type = !int} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 34 : i64,
              constant_value = "7", semantic_type = !int} {}
        }
      }
    }
  }
}

// REPAIR-NOT: changing smallest negative limit -5 ticks
// REPAIR: warning: IEEE 1800-2017 31.9.1 mutually inconsistent delayed-signal constraints; changing smallest negative limit -2 ticks to 0 and recalculating
// IR-LABEL: obelisk_sim.func private @unit_0
// IR-SAME: obelisk_sim.timing_check_arg_ticks = array<i64: 0, 0, 0, 8>
// IR-LABEL: obelisk_sim.func private @unit_1
// IR-SAME: obelisk_sim.timing_check_arg_ticks = array<i64: 0, 0, 2, 1>
// IR-DAG: obelisk_sim.timing_check_arg_ticks = array<i64: 0, 0, 1, 1>
// IR-DAG: obelisk_sim.time.constant 3
// IR-DAG: obelisk_sim.time.constant 6
// IR-NOT: timing_check_table

// An adjusted-zero endpoint remains open.  A simultaneous reference/data
// occurrence has delta zero and is explicitly excluded before notification.
// ZERO-LABEL: obelisk_sim.func private @unit_0
// ZERO: %[[NOW:.*]] = obelisk_sim.time.now
// ZERO: %[[DELTA:.*]] = arith.subi %[[NOW]],
// ZERO: arith.cmpi ult, %[[DELTA]],
// ZERO: %[[NONZERO:.*]] = arith.cmpi ne, %[[DELTA]],
// ZERO: arith.andi {{.*}}, %[[NONZERO]]
