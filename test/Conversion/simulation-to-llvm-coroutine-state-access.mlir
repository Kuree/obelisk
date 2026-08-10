// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @state_access {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "state_access.access"

    obelisk_sim.func @access(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 1 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 1 : i32},
        %value: !obelisk_sim.logic<8>
            {obelisk_sim.capture_kind = 2 : i32})
        -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %loaded = obelisk_sim.ref.load %ref :
          !obelisk_sim.ref<!obelisk_sim.logic<8>> ->
          !obelisk_sim.logic<8>
      obelisk_sim.ref.store %value to %ref :
          !obelisk_sim.logic<8>,
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %value to %ref {obelisk_sim.continuous_store} :
          !obelisk_sim.logic<8>,
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %net_value = obelisk_sim.net.read %net :
          !obelisk_sim.net<!obelisk_sim.logic<8>> ->
          !obelisk_sim.logic<8>
      obelisk_sim.return %loaded : !obelisk_sim.logic<8>
    }
  }
}

// CHECK-LABEL: llvm.func @access
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: %[[OLD_VALUE:.*]] = llvm.load
// CHECK: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: %[[OLD_UNKNOWN:.*]] = llvm.load
// CHECK: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: %[[VALUE_CHANGED_BYTE:.*]] = llvm.load
// CHECK: %[[VALUE_CHANGED:.*]] = llvm.icmp "ne" %[[VALUE_CHANGED_BYTE]]
// CHECK: %[[CANDIDATE_VALUE:.*]] = llvm.select %[[VALUE_CHANGED]], %arg3, %[[OLD_VALUE]]
// CHECK: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: %[[UNKNOWN_CHANGED_BYTE:.*]] = llvm.load
// CHECK: %[[UNKNOWN_CHANGED:.*]] = llvm.icmp "ne" %[[UNKNOWN_CHANGED_BYTE]]
// CHECK: llvm.select %[[UNKNOWN_CHANGED]], %arg4, %[[OLD_UNKNOWN]]
// CHECK: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: %[[VISIBLE_VALUE:.*]] = llvm.load
// CHECK: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: %[[VISIBLE_UNKNOWN:.*]] = llvm.load
// CHECK: llvm.store %[[OLD_VALUE]]
// CHECK: llvm.store %[[OLD_UNKNOWN]]
// CHECK: llvm.store %[[VISIBLE_VALUE]]
// CHECK: llvm.store %[[VISIBLE_UNKNOWN]]
// CHECK: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: llvm.call @obelisk_rt_v1_native_state_store_continuous_plane
// CHECK: llvm.select
// CHECK: llvm.call @obelisk_rt_v1_native_state_store_continuous_plane
// CHECK: llvm.select
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// CHECK-NOT: obelisk_sim.ref.
// CHECK-NOT: obelisk_sim.net.read
