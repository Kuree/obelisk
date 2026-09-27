// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @event_input_store {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 port_input hierarchy "top.wake"
        {internal}
    simulation.storage.decl 0 in 0 : !simulation.event design
        hierarchy "top.wake"

    simulation.func @port(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.event {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 9 : i32, internal} {
      %storage = simulation.context.storage %ctx[0]
          : !simulation.ref<!simulation.event>
      simulation.ref.store %value to %storage
          : !simulation.event, !simulation.ref<!simulation.event>
      simulation.return
    }
  }
}

// StoreState is opcode 28. Event-handle publication uses flags 0, not the
// packed continuous-driver flag 2.
// CHECK: obelisk.bytecode.image = array<i8:
// CHECK-SAME: {{.*}}28, 0, 0, 0, 0, 0, 0, 0
// NATIVE: llvm.call @obelisk_rt_v1_native_state_store_plane
// NATIVE-NOT: llvm.call @obelisk_rt_v1_native_state_store_continuous_plane
