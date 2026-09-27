// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --mlir-disable-threading --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// Actor slots follow root spawn order: root=0, z_always=1, a_initial=2.
// This legal schedule orders the independent children a_initial, z_always.
// Splitting each packed capture into value/unknown arguments replaces its
// entry block. Native node ordering must retain the captured semantic rank,
// matching the spawn helper, rather than query stale block-pointer keys.
// Without the fix, both new entry blocks can receive fallback rank zero and
// the native node table incorrectly becomes slot order 0, 1, 2.

// CHECK-LABEL: llvm.mlir.global internal constant @__obelisk_aot_schedule_nodes_v1()
// CHECK-SAME: !llvm.array<3 x struct<(i32, i32, i32)>>
// CHECK: %[[ROOT:.*]] = llvm.mlir.constant(0 : i32)
// CHECK-NEXT: llvm.insertvalue %[[ROOT]], {{.*}}[0]
// CHECK: llvm.insertvalue {{.*}}[0] : !llvm.array<3 x struct<(i32, i32, i32)>>
// CHECK: %[[FIRST:.*]] = llvm.mlir.constant(2 : i32)
// CHECK-NEXT: llvm.insertvalue %[[FIRST]], {{.*}}[0]
// CHECK: llvm.insertvalue {{.*}}[1] : !llvm.array<3 x struct<(i32, i32, i32)>>
// CHECK: %[[SECOND:.*]] = llvm.mlir.constant(1 : i32)
// CHECK-NEXT: llvm.insertvalue %[[SECOND]], {{.*}}[0]
// CHECK: llvm.insertvalue {{.*}}[2] : !llvm.array<3 x struct<(i32, i32, i32)>>
// CHECK: llvm.return

// CHECK-LABEL: llvm.func @a_initial.__obelisk_spawn
// CHECK: %[[INITIAL_SLOT:.*]] = llvm.mlir.constant(2 : i32)
// CHECK-NEXT: %[[INITIAL_RANK:.*]] = llvm.mlir.constant(1 : i32)
// CHECK: llvm.call @obelisk_rt_v1_scheduler_add_aot({{.*}}, %[[INITIAL_SLOT]], %[[INITIAL_RANK]],
// CHECK-LABEL: llvm.func @z_always.__obelisk_spawn
// CHECK: %[[ALWAYS_SLOT:.*]] = llvm.mlir.constant(1 : i32)
// CHECK-NEXT: %[[ALWAYS_RANK:.*]] = llvm.mlir.constant(2 : i32)
// CHECK: llvm.call @obelisk_rt_v1_scheduler_add_aot({{.*}}, %[[ALWAYS_SLOT]], %[[ALWAYS_RANK]],

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 2 : i32
} {
  obelisk_sim.design @schedule attributes {
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 1, function = @a_initial, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 2, function = @z_always, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>],
      edges = [], regions = [#schedule.region<kind = active, groups = [
        #schedule.group<fragments = [0], schedule = acyclic, feedback = []>,
        #schedule.group<fragments = [1], schedule = acyclic, feedback = []>,
        #schedule.group<fragments = [2], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "a_initial"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "z_always"
    obelisk_sim.scope.decl 0

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %always = obelisk_sim.spawn @z_always(%ctx, %value) :
          !obelisk_sim.context, !obelisk_sim.logic<8> -> !obelisk_sim.process
      %initial = obelisk_sim.spawn @a_initial(%ctx, %value) :
          !obelisk_sim.context, !obelisk_sim.logic<8> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @a_initial(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %capture: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      obelisk_sim.return
    }

    obelisk_sim.func @z_always(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %capture: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.return
    }
  }
}
