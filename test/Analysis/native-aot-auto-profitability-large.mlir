// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-native-aot-analysis)' 2>&1 | FileCheck %s

// The compute graph of native-aot-auto-profitability.mlir with
// a costly fragment: the same design becomes profitable for native AOT.
// CHECK: native-aot eligible=true fully=false selected=true periodic=false cost=1502/1504

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 0 : i32} {
  simulation.design @auto_profitability attributes {compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1, nodes = [#schedule.fragment<id = 0, function = @dynamic, block = 0, region = active, action = terminate, tier = native, cost = 0, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 1, function = @native, block = 0, region = active, action = terminate, tier = native, cost = 1500, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 2, function = @root, block = 0, region = active, action = terminate, tier = native, cost = 2, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 3, function = @spawner, block = 0, region = active, action = terminate, tier = native, cost = 1, lane = 0, twoState = true, effects = []>], edges = [#schedule.edge<source = 2, target = 1, kind = spawn>, #schedule.edge<source = 2, target = 3, kind = spawn>, #schedule.edge<source = 3, target = 0, kind = spawn>], regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [0], schedule = acyclic, feedback = []>, #schedule.group<fragments = [1], schedule = acyclic, feedback = []>, #schedule.group<fragments = [2], schedule = acyclic, feedback = []>, #schedule.group<fragments = [3], schedule = acyclic, feedback = []>]>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "native"
    simulation.code_unit.decl 3 in 0 initial hierarchy "spawner"
    simulation.code_unit.decl 4 in 0 initial hierarchy "dynamic"
    simulation.func @root(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 0 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [2]>, home_region = 2 : i32} {
      %0 = simulation.spawn @native(%arg0) : !simulation.context -> !simulation.process
      %1 = simulation.spawn @spawner(%arg0) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @native(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 2 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 1 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [1]>, home_region = 2 : i32} {
      %c0_i32 = arith.constant 0 : i32
      %c1_i32 = arith.constant 1 : i32
      %0 = arith.addi %c0_i32, %c1_i32 : i32
      %1 = arith.addi %c1_i32, %0 : i32
      %2 = arith.addi %0, %1 : i32
      simulation.return
    }
    simulation.func @spawner(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 3 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 1 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [3]>, home_region = 2 : i32} {
      %0 = simulation.spawn @dynamic(%arg0) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @dynamic(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 4 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 1 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0]>, home_region = 2 : i32} {
      simulation.return
    }
  }
}

