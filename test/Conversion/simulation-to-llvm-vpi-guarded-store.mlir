// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=OFF

// A clean procedural store must include its publication in the fast path.
// Checking only direct plane stores misses capability-only generic fanout.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 2 : i32
} {
  obelisk_sim.design @guarded_store {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "guarded_store.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "guarded_store.initial"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "guarded_store.watcher"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "guarded_store.write_q"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %q = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %w = obelisk_sim.spawn @watcher(%ctx, %q) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      %p = obelisk_sim.spawn @initial(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      obelisk_sim.call @write_q(%ctx, %zero) : (!obelisk_sim.context, !obelisk_sim.logic<32>) -> ()
      obelisk_sim.return
    }
    obelisk_sim.func @watcher(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %q: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %q to ^resume {site = #obelisk_sim.continuation<id = 1>} : !obelisk_sim.ref<!obelisk_sim.logic<32>>
    ^resume:
      cf.br ^wait
    }
    obelisk_sim.func @write_q(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<32> {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %q = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.ref.store %value to %q : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.return
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
