// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=OFF

// Writable VPI is a capability, not an active consumer. Fixed net reads retain
// guarded direct access. A whole-driver fast path needs the GLOBAL clean flag,
// not merely a clean driver root: a forced net still needs its unforced driver
// contribution retained in canonical storage for release.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 2 : i32
} {
  obelisk_sim.design @guarded_net {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "guarded_net.read"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "guarded_net.drive"
    obelisk_sim.code_unit.decl 3 in 0 root_initializer hierarchy "guarded_net.root"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "guarded_net.initial"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<32> design
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 3 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      obelisk_sim.call @drive_net(%ctx, %zero) : (!obelisk_sim.context, !obelisk_sim.logic<32>) -> ()
      %value = obelisk_sim.call @read_net(%ctx) : (!obelisk_sim.context) -> !obelisk_sim.logic<32>
      obelisk_sim.return
    }
    obelisk_sim.func @read_net(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> !obelisk_sim.logic<32>
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<32>>
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.return %value : !obelisk_sim.logic<32>
    }
    obelisk_sim.func @drive_net(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<32> {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<32>>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<32>>, !obelisk_sim.logic<32>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @read_net(
// CHECK: llvm.mlir.addressof @__obelisk_static_specialization_fast_v1
// CHECK: llvm.cond_br
// CHECK: llvm.call @obelisk_rt_v1_static_specialization_guard
// CHECK: llvm.mlir.addressof @__obelisk_state_value
// CHECK: llvm.load
// CHECK: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-LABEL: llvm.func @drive_net(
// CHECK: llvm.mlir.addressof @__obelisk_static_specialization_fast_v1
// CHECK: llvm.cond_br
// CHECK-NOT: llvm.call @obelisk_rt_v1_static_specialization_guard
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: llvm.mlir.addressof @__obelisk_state_value
// CHECK: llvm.store
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK: llvm.call @obelisk_rt_v1_native_state_store_plane

// OFF-LABEL: llvm.func @read_net(
// OFF-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// OFF: llvm.return
// OFF-LABEL: llvm.func @drive_net(
// OFF-NOT: llvm.call @obelisk_rt_v1_native_state_store_plane
// OFF-NOT: llvm.mlir.addressof @__obelisk_static_specialization_fast_v1
// OFF: llvm.call @obelisk_rt_v1_scheduler_static_transition
