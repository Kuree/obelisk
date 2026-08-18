// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// A standalone simulator's exit status is the scheduler's, and nothing else
// gets to report it: the context is destroyed on the next line and the process
// exits. IEEE 1800-2017 says nothing about how a tool reports a run that
// failed, so `main` says what went wrong before it returns the status, leaving
// $finish (status 0) and $fatal (which already printed its own message) quiet.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @design attributes {compute_graph = #obelisk_sim.graph<version = 1, vpi = off, workers = 1, nodes = [#obelisk_sim.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 1, lane = 0, twoState = true, effects = []>, #obelisk_sim.fragment<id = 1, function = @unit_0, block = 0, region = active, action = terminate, tier = native, cost = 2, lane = 0, twoState = true, effects = []>], edges = [#obelisk_sim.edge<source = 0, target = 1, kind = spawn>], regions = [#obelisk_sim.region<kind = active, groups = [#obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>, #obelisk_sim.group<fragments = [1], schedule = acyclic, feedback = []>]>, #obelisk_sim.region<kind = nba, groups = []>, #obelisk_sim.region<kind = observed, groups = []>, #obelisk_sim.region<kind = reactive, groups = []>, #obelisk_sim.region<kind = postponed, groups = []>]>, time_precision_fs = 1000000 : i64} {
    obelisk_sim.scope.decl 0 hierarchy "\\$root " debug "$root" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    obelisk_sim.scope.decl 1 parent 0 hierarchy "scheduler_main" debug "scheduler_main" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    obelisk_sim.code_unit.decl 832639515527371617 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer"
    obelisk_sim.code_unit.decl 5488801650660482716 in 1 initial hierarchy "scheduler_main.$code_unit_5" debug ""
    obelisk_sim.func @__obelisk_root(%arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {code_unit_id = 832639515527371617 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 0 : i32, fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = [0]>, home_region = 2 : i32} {
      %0 = obelisk_sim.spawn @unit_0(%arg0) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @unit_0(%arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {code_unit_id = 5488801650660482716 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 1 : i32, fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = [1]>, home_region = 2 : i32, obelisk_sim.hierarchical_name = "scheduler_main"} {
      %c1_i32 = arith.constant 1 : i32
      obelisk_sim.finish %arg0, %c1_i32
      obelisk_sim.return
    }
  }
}


// CHECK-LABEL: llvm.func @main
// CHECK:      %[[STATUS:.*]] = llvm.call @obelisk_rt_v1_scheduler_run
// CHECK-NEXT: llvm.call @obelisk_rt_v1_scheduler_report_status(%{{.*}}, %[[STATUS]])
// CHECK-NEXT: llvm.call @obelisk_rt_v1_context_destroy
// CHECK-NEXT: llvm.return %[[STATUS]]
