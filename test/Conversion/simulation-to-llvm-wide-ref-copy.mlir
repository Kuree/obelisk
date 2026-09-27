// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @wide_ref_copy {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "wide_ref_copy.copy"

    simulation.func @copy(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<65>>
            {simulation.capture_kind = 1 : i32},
        %destination: !simulation.ref<!simulation.logic<65>>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %value = simulation.ref.load %source :
          !simulation.ref<!simulation.logic<65>> ->
          !simulation.logic<65>
      simulation.ref.store %value to %destination :
          !simulation.logic<65>,
          !simulation.ref<!simulation.logic<65>>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @copy
// CHECK-COUNT-4: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// CHECK-NOT: i65
// CHECK-NOT: simulation.ref.
// CHECK: llvm.return
