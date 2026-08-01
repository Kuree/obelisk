// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @driver_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "driver_lowering.drive"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.driver.decl 0 in 0 drives 0 :
        !obelisk_sim.logic<2> design

    obelisk_sim.func @drive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %value = obelisk_sim.logic.constant 1 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @drive
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: llvm.mlir.addressof @__obelisk_state_value
// CHECK: llvm.load
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @__obelisk_state_unknown
// CHECK: llvm.store
// CHECK: llvm.cond_br
// CHECK: %[[ROOT:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK: %[[WIDTH:.*]] = llvm.mlir.constant(2 : i64) : i64
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_scheduler_static_transition({{.*}}, %[[ROOT]], %[[OFFSET]], %[[WIDTH]], {{.*}})
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// CHECK-NOT: obelisk_sim.driver.drive
