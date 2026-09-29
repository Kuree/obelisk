// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=OFF

// Writable VPI is a capability, not an active consumer. Fixed net reads retain
// guarded direct access. A whole-driver fast path needs the GLOBAL clean flag,
// not merely a clean driver root: a forced net still needs its unforced driver
// contribution retained in canonical storage for release.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 2 : i32
} {
  simulation.design @guarded_net {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "guarded_net.read"
    simulation.code_unit.decl 2 in 0 function hierarchy "guarded_net.drive"
    simulation.code_unit.decl 3 in 0 root_initializer hierarchy "guarded_net.root"
    simulation.code_unit.decl 4 in 0 initial hierarchy "guarded_net.initial"
    simulation.net.decl 0 in 0 : !simulation.logic<32> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<32> design
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 3 : i64} {
      %process = simulation.spawn @initial(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      simulation.call @drive_net(%ctx, %zero) : (!simulation.context, !simulation.logic<32>) -> ()
      %value = simulation.call @read_net(%ctx) : (!simulation.context) -> !simulation.logic<32>
      simulation.return
    }
    simulation.func @read_net(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.logic<32>
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<32>>
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.return %value : !simulation.logic<32>
    }
    simulation.func @drive_net(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<32> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<32>>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<32>>, !simulation.logic<32>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @read_net(
// CHECK: llvm.mlir.addressof @__obelisk_state_specialization_fast_v1
// CHECK: llvm.cond_br
// CHECK: llvm.call @obelisk_rt_v1_static_specialization_guard
// CHECK: llvm.mlir.addressof @__obelisk_state_value
// CHECK: llvm.load
// CHECK: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-LABEL: llvm.func @drive_net(
// CHECK: llvm.mlir.addressof @__obelisk_state_specialization_fast_v1
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
// OFF-NOT: llvm.mlir.addressof @__obelisk_state_specialization_fast_v1
// OFF: llvm.call @obelisk_rt_v1_scheduler_static_transition
