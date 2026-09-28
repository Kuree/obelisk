// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph),encode-obelisk-sim-to-bytecode{prune-native=true vpi=full})' | FileCheck %s --check-prefix=PRUNED
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph),encode-obelisk-sim-to-bytecode{vpi=full})' | FileCheck %s --check-prefix=ALL
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph),encode-obelisk-sim-to-bytecode{prune-native=true require-bytecode=true vpi=full})' | FileCheck %s --check-prefix=ALL
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph),encode-obelisk-sim-to-bytecode{vpi=full},encode-obelisk-sim-to-bytecode{prune-native=true vpi=full})' | FileCheck %s --check-prefix=PRUNED

// IEEE 1800-2023 4.5, 4.6, 6.8: state metadata survives omission of native
// bodies, including a native bootstrap excluded from AOT by managed captures.
// PRUNED: obelisk.bytecode.image
// PRUNED-SAME: obelisk.design.database
// PRUNED-NOT: obelisk.bytecode.function
// ALL: simulation.func @root
// ALL-SAME: obelisk.bytecode.function
// ALL: simulation.func @process
// ALL-SAME: obelisk.bytecode.function

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @direct_generic {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "direct_generic.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "direct_generic.process"
    simulation.storage.decl 0 in 0 : !simulation.logic<128> design
    simulation.storage.decl 1 in 0 : !simulation.string design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %managed = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.string>
      %storage = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<128>>
      %process = simulation.spawn @process(%ctx, %storage) :
          !simulation.context, !simulation.ref<!simulation.logic<128>>
          -> !simulation.process
      simulation.return
    }

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<128>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<128>> -> !simulation.logic<128>
      simulation.ref.store %value to %state :
          !simulation.logic<128>, !simulation.ref<!simulation.logic<128>>
      simulation.return
    }
  }
}

