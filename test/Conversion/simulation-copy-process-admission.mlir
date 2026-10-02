// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines --mlir-disable-threading > %t.serial
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines > %t.threaded
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s --check-prefix=ABSENT < %t.serial
// ABSENT-NOT: llvm.func @input_copy.__obelisk_group_body
// ABSENT-NOT: llvm.func @output_copy.__obelisk_group_body
// ABSENT-NOT: llvm.func @continuous_copy.__obelisk_group_body
// ABSENT-NOT: llvm.func @input_copy.__obelisk_table_body
// ABSENT-NOT: llvm.func @output_copy.__obelisk_table_body
// ABSENT-NOT: llvm.func @continuous_copy.__obelisk_table_body

// Admission retains the exact source store and wait. Actor and continuation
// descriptors survive. Schedule-selected transfer implementations share a kernel. Distinct
// continuation IDs remain in the original actors' wait tables.
!word = !simulation.logic<65>
!ref = !simulation.ref<!word>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @copies {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.code_unit.decl 1 in 0 port_input hierarchy "input_copy"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "output_copy"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "continuous_copy"
    simulation.code_unit.decl 4 in 0 port_input hierarchy "wrong_watch"
    simulation.code_unit.decl 5 in 0 port_input hierarchy "side_effect"
    simulation.code_unit.decl 6 in 0 port_input hierarchy "conversion"
    simulation.code_unit.decl 7 in 0 initial hierarchy "initial_copy"
    simulation.code_unit.decl 8 in 0 port_input hierarchy "carried"
    simulation.func @input_copy(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 1 : i64} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %source : !ref -> !word
      simulation.ref.store %value to %sink : !word, !ref
      simulation.suspend.change %source to ^loop : !ref
    }
    simulation.func @output_copy(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %source : !ref -> !word
      simulation.ref.store %value to %sink : !word, !ref
      simulation.suspend.change %source to ^loop {site = #schedule.continuation<id = 7>} : !ref
    }
    simulation.func @continuous_copy(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      cf.br ^loop
    ^loop:
      simulation.ref.copy %source to %sink : !ref
      simulation.suspend.change %source to ^loop : !ref
    }
    simulation.func @wrong_watch(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 4 : i64} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %source : !ref -> !word
      simulation.ref.store %value to %sink : !word, !ref
      simulation.suspend.change %sink to ^loop : !ref
    }
    simulation.func @side_effect(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 5 : i64} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %source : !ref -> !word
      simulation.dump.flush %ctx : (!simulation.context) -> ()
      simulation.ref.store %value to %sink : !word, !ref
      simulation.suspend.change %source to ^loop : !ref
    }
    simulation.func @conversion(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 6 : i64} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %source : !ref -> !word
      %narrow = simulation.logic.resize %value signed = false : !word -> !simulation.logic<64>
      %changed = simulation.logic.resize %narrow signed = false : !simulation.logic<64> -> !word
      simulation.ref.store %changed to %sink : !word, !ref
      simulation.suspend.change %source to ^loop : !ref
    }
    simulation.func @initial_copy(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %source : !ref -> !word
      simulation.ref.store %value to %sink : !word, !ref
      simulation.suspend.change %source to ^loop : !ref
    }
    simulation.func @carried(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 9 : i32, code_unit_id = 8 : i64} {
      %initial = simulation.ref.load %source : !ref -> !word
      cf.br ^loop(%initial : !word)
    ^loop(%previous: !word):
      %value = simulation.ref.load %source : !ref -> !word
      simulation.ref.store %previous to %sink : !word, !ref
      simulation.suspend.change %source to ^loop(%value : !word) : !ref
    }
  }
}

// CHECK-LABEL: llvm.mlir.global internal constant @output_copy.__obelisk_table.plan
// CHECK: llvm.mlir.addressof @__obelisk_transfer_kernel_0.impl.__obelisk_table_body
// CHECK-LABEL: llvm.mlir.global internal constant @output_copy.__obelisk_table.waits
// CHECK: llvm.mlir.constant(7 : i32)
// CHECK-LABEL: llvm.mlir.global internal constant @input_copy.__obelisk_table.plan
// CHECK: llvm.mlir.addressof @__obelisk_transfer_kernel_0.impl.__obelisk_table_body
// CHECK-LABEL: llvm.mlir.global internal constant @continuous_copy.__obelisk_table.plan
// CHECK: llvm.mlir.addressof @__obelisk_transfer_kernel_0.impl.__obelisk_table_body
// CHECK-LABEL: llvm.func @wrong_watch.__obelisk_table_body
// CHECK-NOT: llvm.intr.coro
// CHECK-LABEL: llvm.func @side_effect.__obelisk_table_body
// CHECK-NOT: llvm.intr.coro
// CHECK-LABEL: llvm.func @conversion.__obelisk_table_body
// CHECK-NOT: llvm.intr.coro
// CHECK-LABEL: llvm.func @initial_copy.__obelisk_table_body
// CHECK-NOT: llvm.intr.coro
// CHECK-LABEL: llvm.func @carried.__obelisk_coro_ramp
// CHECK: llvm.intr.coro.begin
// CHECK-LABEL: llvm.func @__obelisk_transfer_kernel_0.impl.__obelisk_table_body
// CHECK-SAME: passthrough = ["noinline"]
// CHECK-NOT: llvm.intr.coro
