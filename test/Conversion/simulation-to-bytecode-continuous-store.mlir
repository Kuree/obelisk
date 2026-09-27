// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @continuous_store {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 port_output hierarchy "top.port"
        {internal}
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
        hierarchy "top.value"

    // Port propagation is a continuous driver even though the store does not
    // need a transient marker attribute to retain that source-level meaning.
    simulation.func @port(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 10 : i32, internal} {
      %value = simulation.logic.constant 42 : i8, 0 : i8
          : !simulation.logic<8>
      %storage = simulation.context.storage %ctx[0]
          : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %storage
          : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
  }
}

// StoreState is opcode 28. Its serialized flags field is 2 for a retained
// continuous publication; source0 and source1 are handle 2 and value 1.
// CHECK: obelisk.bytecode.image = array<i8:
// CHECK-SAME: {{.*}}28, 0, 2, 0, 0, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0
// NATIVE-COUNT-2: llvm.call @obelisk_rt_v1_native_state_store_continuous_plane
