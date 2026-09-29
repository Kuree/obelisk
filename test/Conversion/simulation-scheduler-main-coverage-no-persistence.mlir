// RUN: obelisk-opt %s --obelisk-sim-prepare-coverage \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// A schema may be present solely to support direct covergroup language
// operations.  Finalize that schema, but do not make its presence an implicit
// request to persist coverage.  The metric-selection attribute is deliberately
// empty here, matching that state at the scheduler boundary.

module attributes {
  obelisk.coverage.metrics = [],
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @design attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#schedule.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 1, lane = 0, twoState = true, effects = []>],
      edges = [],
      regions = [
        #schedule.region<kind = active, groups = [#schedule.group<fragments = [0], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>
      ]>,
    time_precision_fs = 1000000 : i64
  } {
    simulation.scope.decl 0 hierarchy "\\$root " debug "$root" {
      dpi_precision_femtoseconds = 1000000 : i64,
      dpi_unit_femtoseconds = 1000000 : i64
    }
    simulation.code_unit.decl 832639515527371617 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer"
    simulation.func @__obelisk_root(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {
      code_unit_id = 832639515527371617 : i64,
      domain = 0 : i32,
      effect_summary = [],
      entry_kind = 0 : i32,
      fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0]>,
      home_region = 2 : i32
    } {
      simulation.return
    }
  }
}

// CHECK: obelisk.execution.coverage_schema_blob
// CHECK-LABEL: llvm.func @main
// CHECK:      llvm.call @obelisk_rt_v1_native_state_initialize
// CHECK:      %[[LINE_COUNT:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK-NEXT: %[[TOGGLE_COUNT:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK-NEXT: %[[PERSIST:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK-NEXT: %[[FINALIZE:.*]] = llvm.call @obelisk_rt_v1_coverage_finalize(%{{.*}}, %[[LINE_COUNT]], %[[TOGGLE_COUNT]], %{{.*}}, %{{.*}}, %[[PERSIST]])
// CHECK:      llvm.call @obelisk_rt_v1_scheduler_run
// CHECK-NOT:  obelisk_rt_v1_coverage_snapshot
