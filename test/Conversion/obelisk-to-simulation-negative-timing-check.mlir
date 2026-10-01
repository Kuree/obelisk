// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --test-obelisk-native-aot-analysis 2>&1 \
// RUN:   | FileCheck %s --check-prefix=AOT
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=ORDER
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=TRANSPORT
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | sed 's/, simulation.negative_timing_delay_commit//' \
// RUN:   | obelisk-opt --test-obelisk-native-aot-analysis 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CERT-MISS
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | sed 's/ {simulation.negative_timing_transport_activation}//' \
// RUN:   | obelisk-opt --test-obelisk-native-aot-analysis 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CERT-MISS

// IEEE 1800-2017 31.9.2's shared delayed-terminal example. At 0.01 ns
// precision the least strict-interior solution is CP=10.01, D=0,
// TI=20.02, TE=2.02. The planner must share CP rather than solve each check
// independently.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.root @root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.instance_body @body attributes {
        hierarchical_name = "top", name = "top", node_id = 2 : i64,
        time_unit_fs = 1000000 : i64,
        time_precision_fs = 10000 : i64} {
      obelisk.sv.symbol.variable @cp attributes {hierarchical_name = "top.cp",
          lifetime = 1 : i32, name = "cp", node_id = 3 : i64,
          semantic_type = !logic1} {}
      obelisk.sv.symbol.variable @d attributes {hierarchical_name = "top.d",
          lifetime = 1 : i32, name = "d", node_id = 4 : i64,
          semantic_type = !logic1} {}
      obelisk.sv.symbol.variable @ti attributes {hierarchical_name = "top.ti",
          lifetime = 1 : i32, name = "ti", node_id = 5 : i64,
          semantic_type = !logic1} {}
      obelisk.sv.symbol.variable @te attributes {hierarchical_name = "top.te",
          lifetime = 1 : i32, name = "te", node_id = 6 : i64,
          semantic_type = !logic1} {}
      obelisk.sv.symbol.variable @enable attributes {hierarchical_name = "top.enable",
          lifetime = 1 : i32, name = "enable", node_id = 50 : i64,
          semantic_type = !logic1} {}
      obelisk.sv.symbol.specify_block @specify attributes {
          hierarchical_name = "top", node_id = 7 : i64
      } {
        obelisk.sv.symbol.system_timing_check @check0 attributes {
            hierarchical_name = "top", node_id = 10 : i64,
            obelisk.basic_timing_check,
            obelisk.negative_timing_check, time_unit_fs = 1000000 : i64,
            time_precision_fs = 10000 : i64, timing_check_kind = 3 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>,
            timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, -10000000, 20000000>} {
          obelisk.sv.expression.named_value attributes {node_id = 11 : i64,
              referenced_path = "top.cp", referenced_symbol = @root::@body::@cp,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 12 : i64,
              referenced_path = "top.d", referenced_symbol = @root::@body::@d,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 13 : i64,
              constant_value = "-10",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 14 : i64,
              constant_value = "20",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.symbol.system_timing_check @check1 attributes {
            hierarchical_name = "top", node_id = 20 : i64,
            obelisk.basic_timing_check,
            obelisk.negative_timing_check, time_unit_fs = 1000000 : i64,
            time_precision_fs = 10000 : i64, timing_check_kind = 3 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>,
            timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, 20000000, -10000000>} {
          obelisk.sv.expression.named_value attributes {node_id = 21 : i64,
              referenced_path = "top.cp", referenced_symbol = @root::@body::@cp,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 22 : i64,
              referenced_path = "top.ti", referenced_symbol = @root::@body::@ti,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 23 : i64,
              constant_value = "20",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 24 : i64,
              constant_value = "-10",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.symbol.system_timing_check @check2 attributes {
            hierarchical_name = "top", node_id = 30 : i64,
            obelisk.basic_timing_check,
            obelisk.negative_timing_check, time_unit_fs = 1000000 : i64,
            time_precision_fs = 10000 : i64, timing_check_kind = 3 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>,
            timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>,
            timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, -4000000, 8000000>} {
          obelisk.sv.expression.named_value attributes {node_id = 31 : i64,
              referenced_path = "top.cp", referenced_symbol = @root::@body::@cp,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 32 : i64,
              referenced_path = "top.te", referenced_symbol = @root::@body::@te,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 33 : i64,
              constant_value = "-4",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 34 : i64,
              constant_value = "8",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        // The same physical CP/TE component is also legal for $recrem.  Its
        // strict bounds select the already-shared CP-TE difference of 7.99.
        obelisk.sv.symbol.system_timing_check @check3 attributes {
            hierarchical_name = "top", node_id = 40 : i64,
            obelisk.basic_timing_check,
            obelisk.negative_timing_check, time_unit_fs = 1000000 : i64,
            time_precision_fs = 10000 : i64, timing_check_kind = 6 : i32,
            timing_check_arg_count = 4 : i64,
            timing_check_arg_has_expression = array<i64: 1, 1, 1, 1>,
            timing_check_arg_has_condition = array<i64: 1, 0, 0, 0>,
            timing_check_arg_expression_children = array<i64: 0, 2, 3, 4>,
            timing_check_arg_condition_children = array<i64: 1, -1, -1, -1>,
            timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>,
            timing_check_arg_is_time = array<i64: 0, 0, 1, 1>,
            timing_check_arg_time_fs = array<i64: 0, 0, 8000000, -7980000>} {
          obelisk.sv.expression.named_value attributes {node_id = 41 : i64,
              referenced_path = "top.cp", referenced_symbol = @root::@body::@cp,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 45 : i64,
              referenced_path = "top.enable",
              referenced_symbol = @root::@body::@enable,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 42 : i64,
              referenced_path = "top.te", referenced_symbol = @root::@body::@te,
              semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 43 : i64,
              constant_value = "8",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 44 : i64,
              constant_value = "-798",
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
      }
    }
  }
}

// CHECK-DAG: simulation.negative_timing_delay_monitor
// CHECK-DAG: simulation.negative_timing_delay_commit
// CHECK-DAG: simulation.time.constant 1001
// CHECK-DAG: simulation.time.constant 2002
// CHECK-DAG: simulation.time.constant 202
// CHECK-DAG: simulation.timing_check_arg_ticks = array<i64: 0, 0, 1, 999>
// CHECK-DAG: simulation.timing_check_arg_ticks = array<i64: 0, 0, 999, 1>
// CHECK-DAG: simulation.timing_check_arg_ticks = array<i64: 0, 0, 399, 1>
// CHECK-DAG: simulation.timing_check_arg_ticks = array<i64: 0, 0, 1, 1>
// CHECK-DAG: simulation.suspend.clock_set
// CHECK-DAG: conditions 1 edges [1, 0] indices [0, -1]
// CHECK-NOT: simulation.suspend.observe
// CHECK-NOT: timing_check_table

// AOT: native-aot eligible=true fully=false
// AOT-SAME: forced_hybrid=true
// AOT-NOT: actor {{[0-9]+}} @__obelisk_negative_timing_monitor_
// AOT: reason negative timing delay monitor requires runtime ordering
// AOT: reason negative timing transport activation

// The AOT exception is conjunctive.  Removing either the target marker or the
// spawn-site marker turns these back into arbitrary non-root dynamic spawns;
// they must lose forced-hybrid certification rather than broadening it.
// CERT-MISS: native-aot eligible=true fully=false
// CERT-MISS-SAME: forced_hybrid=false
// CERT-MISS: reason dynamic spawn multiplicity

// Delayed copies are initialized from their physical sources before either
// their change monitors or the timing-check coordinators can run.  The monitor
// entry itself goes directly to its wait, so initialization is not mistaken
// for a source occurrence.
// ORDER-LABEL: simulation.func @__obelisk_root
// ORDER: simulation.ref.store
// ORDER-NEXT: simulation.spawn @__obelisk_negative_timing_monitor_
// ORDER: simulation.ref.store
// ORDER-NEXT: simulation.spawn @__obelisk_negative_timing_monitor_
// ORDER: simulation.ref.store
// ORDER-NEXT: simulation.spawn @__obelisk_negative_timing_monitor_
// ORDER-NOT: simulation.ref.store
// ORDER: simulation.spawn @unit_0
// ORDER-LABEL: simulation.func private @__obelisk_negative_timing_monitor_5(
// ORDER: ^bb1:
// ORDER-NEXT: simulation.suspend.change
// ORDER: ^bb2:
// ORDER-NEXT: %{{.*}} = simulation.ref.load
// ORDER-NEXT: %{{.*}} = simulation.spawn @__obelisk_negative_timing_monitor_5.$commit

// Every source transition creates an independent one-shot delayed commit and
// immediately returns the monitor to its wait.  There is no inertial
// cancellation slot, so multiple outstanding (including equal-maturity)
// transport copies remain scheduler-visible.
// TRANSPORT-LABEL: simulation.func private @__obelisk_negative_timing_monitor_5.$commit
// TRANSPORT: simulation.suspend.delay
// TRANSPORT: simulation.ref.store
// TRANSPORT-NEXT: simulation.return
// TRANSPORT-LABEL: simulation.func private @__obelisk_negative_timing_monitor_5(
// TRANSPORT: ^bb2:
// TRANSPORT-NEXT: %{{.*}} = simulation.ref.load
// TRANSPORT-NEXT: %{{.*}} = simulation.spawn @__obelisk_negative_timing_monitor_5.$commit
// TRANSPORT-NEXT: cf.br ^bb1
