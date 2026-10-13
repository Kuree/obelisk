// RUN: obelisk-opt %s --obelisk-sim-build-compute-graph --obelisk-sim-specialize-static-state-nba \
// RUN:   --prepare-native-schedule-inputs --schedule-plan-native-state \
// RUN:   --prepare-native-process-frames --schedule-plan-native-transfers \
// RUN:   --schedule-share-native-transfers \
// RUN:   | FileCheck %s --check-prefix=BOUND
// RUN: obelisk-opt %s --obelisk-sim-build-compute-graph --obelisk-sim-specialize-static-state-nba \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=LOWERED
// RUN: obelisk-opt %s --obelisk-sim-build-compute-graph --obelisk-sim-specialize-static-state-nba \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 0, width = 8, direct = true, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 0, width = 8, direct = false, guarded = true, nba = false>"' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 1, width = 8, direct = true, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 1, width = 8, direct = false, guarded = true, nba = false>"' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 2, width = 8, direct = true, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 2, width = 8, direct = false, guarded = true, nba = false>"' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 3, width = 8, direct = true, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 3, width = 8, direct = false, guarded = true, nba = false>"' \
// RUN:   --prepare-native-schedule-inputs --schedule-plan-native-state \
// RUN:   --prepare-native-process-frames --schedule-plan-native-transfers \
// RUN:   --schedule-share-native-transfers \
// RUN:   | FileCheck %s --check-prefix=BOUND
// RUN: obelisk-opt %s --obelisk-sim-build-compute-graph --obelisk-sim-specialize-static-state-nba \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 0, width = 8, direct = true, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 0, width = 8, direct = false, guarded = false, nba = false>"' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 1, width = 8, direct = true, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 1, width = 8, direct = false, guarded = false, nba = false>"' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 2, width = 8, direct = true, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 2, width = 8, direct = false, guarded = false, nba = false>"' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="[#schedule.static_actor_root<function = @first, descriptor = 0, read = true, write = false>, #schedule.static_actor_root<function = @first, descriptor = 1, read = false, write = true>, #schedule.static_actor_root<function = @second, descriptor = 1, read = true, write = false>, #schedule.static_actor_root<function = @second, descriptor = 2, read = false, write = true>]" replace-to=[]' \
// RUN:   -o %t.mixed
// RUN: obelisk-opt %t.mixed \
// RUN:   --prepare-native-schedule-inputs --schedule-plan-native-state \
// RUN:   --prepare-native-process-frames --schedule-plan-native-transfers \
// RUN:   --schedule-share-native-transfers \
// RUN:   | FileCheck %s --check-prefix=SHARED
// RUN: obelisk-opt %t.mixed \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 0, width = 8, direct = false, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 0, width = 8, direct = true, guarded = false, nba = false>"' \
// RUN:   --prepare-native-schedule-inputs --schedule-plan-native-state \
// RUN:   --prepare-native-process-frames --schedule-plan-native-transfers \
// RUN:   --schedule-share-native-transfers \
// RUN:   | FileCheck %s --check-prefix=SOURCE
// RUN: obelisk-opt %t.mixed \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 2, width = 8, direct = false, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 2, width = 8, direct = true, guarded = false, nba = false>"' \
// RUN:   --prepare-native-schedule-inputs --schedule-plan-native-state \
// RUN:   --prepare-native-process-frames --schedule-plan-native-transfers \
// RUN:   --schedule-share-native-transfers \
// RUN:   | FileCheck %s --check-prefix=DESTINATION
// RUN: not obelisk-opt %t.mixed \
// RUN:   --prepare-native-schedule-inputs --schedule-plan-native-state \
// RUN:   --prepare-native-process-frames --schedule-plan-native-transfers \
// RUN:   --schedule-share-native-transfers \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 0, width = 8, direct = false, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 0, width = 8, direct = true, guarded = false, nba = false>"' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 1, width = 8, direct = false, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 1, width = 8, direct = true, guarded = false, nba = false>"' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.static_state_root<descriptor = 2, width = 8, direct = false, guarded = false, nba = false>" replace-to="#schedule.static_state_root<descriptor = 2, width = 8, direct = true, guarded = false, nba = false>"' \
// RUN:   --schedule-plan-native-actors --schedule-specialize-native-captures \
// RUN:   --convert-simulation-to-native-schedule --prepare-native-managed-roots \
// RUN:   --schedule-mark-clean-native-nba --schedule-specialize-native-eval \
// RUN:   --schedule-prepare-native-transfer-kernels -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=STALE

// IEEE 1800-2023 4.9.1/4.9.6 and 9.4.2 require the same activation and
// publication behavior after sharing. Replacing descriptor-bound accesses
// with opaque runtime calls invalidated the generated schedule's NBA ownership
// assumptions in RV32IM. A matching value/capture ABI is insufficient. Guarded
// roots (third RUN) still carry descriptor-bound accesses.
// BOUND-LABEL: module
// BOUND-NOT: schedule.transfer.
// LOWERED-NOT: __obelisk_transfer_kernel
// LOWERED: llvm.func @first.__obelisk_group_body
// LOWERED-NOT: __obelisk_transfer_kernel
// LOWERED: llvm.func @second.__obelisk_group_body
// LOWERED-NOT: __obelisk_transfer_kernel

// With generic captures on the copied roots, an unrelated specialized root
// must not disable all transfer sharing.
// SHARED: schedule.transfer.activation
// SHARED-SAME: actor = @copies::@first
// SHARED-SAME: kernel = @__obelisk_transfer_kernel_0
// SHARED: schedule.transfer.activation
// SHARED-SAME: actor = @copies::@second
// SHARED-SAME: kernel = @__obelisk_transfer_kernel_0
// SHARED: schedule.transfer.kernel
// SHARED-SAME: sym_name = "__obelisk_transfer_kernel_0"

// A direct source root keeps only the copy that reads it unshared.
// SOURCE-LABEL: module
// SOURCE-NOT: schedule.transfer.activation {{.*}}actor = @copies::@first
// SOURCE: schedule.transfer.activation
// SOURCE-SAME: actor = @copies::@second
// SOURCE-NOT: schedule.transfer.

// A direct destination root keeps only the copy that writes it unshared.
// DESTINATION-LABEL: module
// DESTINATION: schedule.transfer.activation
// DESTINATION-SAME: actor = @copies::@first
// DESTINATION-NOT: schedule.transfer.

// Once every root is direct again, the shared plan would erase the
// descriptor-bound storage accesses the specialized bodies rely on.
// STALE: transfer plan cannot erase specialized storage bindings

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
