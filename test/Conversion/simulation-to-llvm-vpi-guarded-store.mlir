// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=OFF

// A clean procedural store must include its publication in the fast path.
// Checking only direct plane stores misses capability-only generic fanout.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 2 : i32
} {
  simulation.design @guarded_store {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "guarded_store.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "guarded_store.initial"
    simulation.code_unit.decl 3 in 0 always hierarchy "guarded_store.watcher"
    simulation.code_unit.decl 4 in 0 function hierarchy "guarded_store.write_q"
    simulation.storage.decl 0 in 0 : !simulation.logic<32> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %q = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<32>>
      %w = simulation.spawn @watcher(%ctx, %q) : !simulation.context, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %p = simulation.spawn @initial(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      simulation.call @write_q(%ctx, %zero) : (!simulation.context, !simulation.logic<32>) -> ()
      simulation.return
    }
    simulation.func @watcher(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %q: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %q to ^resume {site = #schedule.continuation<id = 1>} : !simulation.ref<!simulation.logic<32>>
    ^resume:
      cf.br ^wait
    }
    simulation.func @write_q(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<32> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %q = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<32>>
      simulation.ref.store %value to %q : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @write_q(
// CHECK: llvm.mlir.addressof @__obelisk_static_specialization_fast_v1
// CHECK: llvm.cond_br
// CHECK-NOT: llvm.call @obelisk_rt_v1_static_specialization_guard
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_{{.*}}_plane
// CHECK: llvm.mlir.addressof @__obelisk_state_value
// CHECK: llvm.store
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_{{.*}}_plane
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK: llvm.call @obelisk_rt_v1_static_specialization_guard
// CHECK: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// OFF-LABEL: llvm.func @write_q(
// OFF-NOT: llvm.mlir.addressof @__obelisk_static_specialization_fast_v1
// OFF-NOT: llvm.call @obelisk_rt_v1_native_state_{{.*}}_plane
// OFF: llvm.call @obelisk_rt_v1_scheduler_static_transition
// OFF: llvm.return
