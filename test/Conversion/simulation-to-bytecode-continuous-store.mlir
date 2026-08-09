// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @continuous_store {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 port_output hierarchy "top.port"
        {internal}
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
        hierarchy "top.value"

    // Port propagation is a continuous driver even though the store does not
    // need a transient marker attribute to retain that source-level meaning.
    obelisk_sim.func @port(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 10 : i32, internal} {
      %value = obelisk_sim.logic.constant 42 : i8, 0 : i8
          : !obelisk_sim.logic<8>
      %storage = obelisk_sim.context.storage %ctx[0]
          : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %value to %storage
          : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }
  }
}

// StoreState is opcode 28. Its serialized flags field is 2 for a retained
// continuous publication; source0 and source1 are handle 2 and value 1.
// CHECK: obelisk.bytecode.image = array<i8:
// CHECK-SAME: {{.*}}28, 0, 2, 0, 0, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0
// NATIVE-COUNT-2: llvm.call @obelisk_rt_v1_native_state_store_continuous_plane
