// RUN: obelisk-opt %s --obelisk-sim-prepare-coverage \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// A standalone simulator's exit status is the scheduler's, and nothing else
// gets to report it: the context is destroyed on the next line and the process
// exits. IEEE 1800-2017 says nothing about how a tool reports a run that
// failed, so `main` says what went wrong before it returns the status, leaving
// $finish (status 0) and $fatal (which already printed its own message) quiet.

module attributes {
  obelisk.coverage.metrics = ["toggle"],
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @design attributes {compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1, nodes = [#schedule.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 1, function = @unit_0, block = 0, region = active, action = terminate, tier = native, cost = 2, lane = 0, twoState = true, effects = []>], edges = [#schedule.edge<source = 0, target = 1, kind = spawn>], regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [0], schedule = acyclic, feedback = []>, #schedule.group<fragments = [1], schedule = acyclic, feedback = []>]>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>, time_precision_fs = 1000000 : i64} {
    simulation.scope.decl 0 hierarchy "\\$root " debug "$root" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    simulation.scope.decl 1 parent 0 hierarchy "scheduler_main" debug "scheduler_main" vpi_kind 32 {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    simulation.code_unit.decl 832639515527371617 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer"
    simulation.code_unit.decl 5488801650660482716 in 1 initial hierarchy "scheduler_main.$code_unit_5" debug ""
    simulation.storage.decl 0 in 1 : i1 design hierarchy "scheduler_main.flag" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"scheduler-main.sv", 1, 1, "scheduler-main.sv", 1, 4, "">
    }
    simulation.func @__obelisk_root(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 832639515527371617 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 0 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0]>, home_region = 2 : i32} {
      %0 = simulation.spawn @unit_0(%arg0) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @unit_0(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 5488801650660482716 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 1 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [1]>, home_region = 2 : i32, simulation.hierarchical_name = "scheduler_main"} {
      %c1_i32 = arith.constant 1 : i32
      simulation.finish %arg0, %c1_i32
      simulation.return
    }
  }
}


// CHECK-LABEL: llvm.func @main
// CHECK:      llvm.call @obelisk_rt_v1_native_state_sync
// CHECK:      %[[PERSIST:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK-NEXT: %[[FINALIZE:.*]] = llvm.call @obelisk_rt_v1_coverage_finalize(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %[[PERSIST]])
// CHECK:      llvm.call @obelisk_rt_v1_coverage_toggle_bind
// CHECK:      llvm.call @obelisk_rt_v1_coverage_toggle_seal
// CHECK:      %[[STATUS:.*]] = llvm.call @obelisk_rt_v1_scheduler_run
// CHECK-NEXT: %[[TIME:.*]] = llvm.call @obelisk_rt_v1_scheduler_time
// CHECK-NEXT: %[[TIME_ADDRESS:.*]] = llvm.mlir.addressof @__obelisk_final_time
// CHECK-NEXT: llvm.store %[[TIME]], %[[TIME_ADDRESS]]
// CHECK-NEXT: llvm.call @obelisk_rt_v1_scheduler_report_status(%{{.*}}, %[[STATUS]])
// CHECK-NEXT: %[[DUMP:.*]] = llvm.call @obelisk_rt_v1_coverage_snapshot
// CHECK-NEXT: %[[OK:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK-NEXT: %[[RUN_SUCCEEDED:.*]] = llvm.icmp "eq" %[[STATUS]], %[[OK]] : i32
// CHECK-NEXT: %[[FINAL_STATUS:.*]] = llvm.select %[[RUN_SUCCEEDED]], %[[DUMP]], %[[STATUS]] : i1, i32
// CHECK:      llvm.call @obelisk_rt_v1_scheduler_report_status(%{{.*}}, %[[DUMP]])
// CHECK-NEXT: llvm.call @obelisk_rt_v1_context_destroy
// CHECK-NEXT: llvm.return %[[FINAL_STATUS]]
