// RUN: %split-file %s %t
// RUN: obelisk-opt %t/input.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-specialize-static-state-nba))' > %t/static.mlir
// RUN: obelisk-opt %t/static.mlir --pass-pipeline='builtin.module(prepare-native-schedule-inputs,schedule-plan-native-state,prepare-native-process-frames,schedule-plan-native-transfers,schedule-share-native-transfers)' | FileCheck %s --check-prefix=BOUND
// RUN: obelisk-opt %t/static.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LOWERED
// RUN: sed 's/direct = true, guarded = false/direct = false, guarded = true/g' %t/static.mlir > %t/guarded.mlir
// RUN: obelisk-opt %t/guarded.mlir --pass-pipeline='builtin.module(prepare-native-schedule-inputs,schedule-plan-native-state,prepare-native-process-frames,schedule-plan-native-transfers,schedule-share-native-transfers)' | FileCheck %s --check-prefix=BOUND
// RUN: sed -f %t/generic-captures.sed %t/static.mlir > %t/mixed.mlir
// RUN: obelisk-opt %t/mixed.mlir --pass-pipeline='builtin.module(prepare-native-schedule-inputs,schedule-plan-native-state,prepare-native-process-frames,schedule-plan-native-transfers,schedule-share-native-transfers)' > %t/shared.mlir
// RUN: FileCheck %s --check-prefix=SHARED < %t/shared.mlir
// RUN: sed 's/\(static_state_root<descriptor = 0, width = 8, \)direct = false/\1direct = true/g' %t/mixed.mlir > %t/source.mlir
// RUN: obelisk-opt %t/source.mlir --pass-pipeline='builtin.module(prepare-native-schedule-inputs,schedule-plan-native-state,prepare-native-process-frames,schedule-plan-native-transfers,schedule-share-native-transfers)' | FileCheck %s --check-prefix=SOURCE
// RUN: sed 's/\(static_state_root<descriptor = 2, width = 8, \)direct = false/\1direct = true/g' %t/mixed.mlir > %t/destination.mlir
// RUN: obelisk-opt %t/destination.mlir --pass-pipeline='builtin.module(prepare-native-schedule-inputs,schedule-plan-native-state,prepare-native-process-frames,schedule-plan-native-transfers,schedule-share-native-transfers)' | FileCheck %s --check-prefix=DESTINATION
// RUN: sed 's/direct = false/direct = true/g' %t/shared.mlir > %t/stale.mlir
// RUN: not obelisk-opt %t/stale.mlir --mlir-print-op-on-diagnostic=false --pass-pipeline='builtin.module(schedule-plan-native-actors,schedule-specialize-native-captures,convert-simulation-to-native-schedule,prepare-native-managed-roots,schedule-mark-clean-native-nba,schedule-specialize-native-eval,schedule-prepare-native-transfer-kernels)' 2>&1 | FileCheck %s --check-prefix=STALE

// IEEE 1800-2023 4.9.1/4.9.6 and 9.4.2 require the same activation and
// publication behavior after sharing. Replacing descriptor-bound accesses
// with opaque runtime calls invalidated the generated schedule's NBA ownership
// assumptions in RV32IM. A matching value/capture ABI is insufficient.
// BOUND-LABEL: module
// BOUND-NOT: schedule.transfer.
// LOWERED-NOT: __obelisk_transfer_kernel
// LOWERED: llvm.func @first.__obelisk_group_body
// LOWERED-NOT: __obelisk_transfer_kernel
// LOWERED: llvm.func @second.__obelisk_group_body
// LOWERED-NOT: __obelisk_transfer_kernel

// An unrelated specialized root must not disable all transfer sharing.
// SHARED: schedule.transfer.activation
// SHARED-SAME: actor = @copies::@first
// SHARED-SAME: kernel = @__obelisk_transfer_kernel_0
// SHARED: schedule.transfer.activation
// SHARED-SAME: actor = @copies::@second
// SHARED-SAME: kernel = @__obelisk_transfer_kernel_0
// SHARED: schedule.transfer.kernel
// SHARED-SAME: sym_name = "__obelisk_transfer_kernel_0"
// SOURCE-LABEL: module
// SOURCE-NOT: schedule.transfer.activation {{.*}}actor = @copies::@first
// SOURCE: schedule.transfer.activation
// SOURCE-SAME: actor = @copies::@second
// SOURCE-NOT: schedule.transfer.
// DESTINATION-LABEL: module
// DESTINATION: schedule.transfer.activation
// DESTINATION-SAME: actor = @copies::@first
// DESTINATION-NOT: schedule.transfer.
// STALE: transfer plan cannot erase specialized storage bindings

//--- generic-captures.sed
s/\(static_state_root<descriptor = [012], width = 8, \)direct = true/\1direct = false/g
s/actorRoots = \[[^]]*\]/actorRoots = []/g

//--- input.mlir
!ref = !simulation.ref<!simulation.logic<8>>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @copies {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 port_input hierarchy "first"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "second"
    simulation.func @first(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %dst: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 1 : i64} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %src : !ref -> !simulation.logic<8>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !ref
      simulation.suspend.change %src to ^loop : !ref
    }
    simulation.func @second(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %dst: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %src : !ref -> !simulation.logic<8>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !ref
      simulation.suspend.change %src to ^loop : !ref
    }
  }
}
