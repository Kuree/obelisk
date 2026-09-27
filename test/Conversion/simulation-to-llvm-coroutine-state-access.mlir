// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @state_access {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "state_access.access"

    simulation.func @access(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 1 : i32},
        %net: !simulation.net<!simulation.logic<8>>
            {simulation.capture_kind = 1 : i32},
        %value: !simulation.logic<8>
            {simulation.capture_kind = 2 : i32})
        -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %loaded = simulation.ref.load %ref :
          !simulation.ref<!simulation.logic<8>> ->
          !simulation.logic<8>
      simulation.ref.store %value to %ref :
          !simulation.logic<8>,
          !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %ref {simulation.continuous_store} :
          !simulation.logic<8>,
          !simulation.ref<!simulation.logic<8>>
      %net_value = simulation.net.read %net :
          !simulation.net<!simulation.logic<8>> ->
          !simulation.logic<8>
      simulation.return %loaded : !simulation.logic<8>
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
// CHECK-NOT: simulation.ref.
// CHECK-NOT: simulation.net.read
