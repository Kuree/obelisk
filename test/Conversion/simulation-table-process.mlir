// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines --mlir-disable-threading -o %t.serial
// RUN: diff %t.mlir %t.serial
// RUN: mlir-translate --mlir-to-llvmir %t.mlir | opt -passes=verify -disable-output

// IEEE 1800-2023 9.4.2: the executed event control selects its wait and edge.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @tables {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i64 design
    simulation.code_unit.decl 9000015 in 0 always hierarchy "test.generated_execution.table_process"
    simulation.func @table_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %sink: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 9000015 : i64} {
      simulation.suspend.change %source to ^choose
          {site = #schedule.continuation<id = 3>, schedule.procedural_event_wait} : !simulation.ref<i64>
    ^choose:
      %value = simulation.ref.load %source : !simulation.ref<i64> -> i64
      %zero = arith.constant 0 : i64
      %first = arith.cmpi eq, %value, %zero : i64
      cf.cond_br %first, ^rise, ^any
    ^rise:
      %eleven = arith.constant 11 : i64
      simulation.ref.store %eleven to %sink : i64, !simulation.ref<i64>
      simulation.suspend.edge posedge %source to ^choose
          {site = #schedule.continuation<id = 7>} : !simulation.ref<i64>
    ^any:
      %twenty_two = arith.constant 22 : i64
      simulation.ref.store %twenty_two to %sink : i64, !simulation.ref<i64>
      simulation.suspend.any %source, %sink edges [0, 2] to ^done
          {site = #schedule.continuation<id = 13>, resume_region = 16 : i32} :
          !simulation.ref<i64>, !simulation.ref<i64>
    ^done:
      simulation.return
    }

  }
}
// CHECK: llvm.mlir.addressof @obelisk_rt_v1_table_process_execute
// CHECK: llvm.mlir.addressof @table_process.__obelisk_table.plan
// CHECK: llvm.mlir.global internal constant @table_process.__obelisk_table.plan
// CHECK: llvm.mlir.addressof @table_process.__obelisk_table_body
// CHECK: llvm.mlir.global internal constant @table_process.__obelisk_table.waits
// CHECK-SAME: !llvm.array<3 x
// CHECK: llvm.mlir.constant(3 : i32)
// CHECK: llvm.mlir.constant(7 : i32)
// CHECK: llvm.mlir.constant(13 : i32)
// CHECK-LABEL: llvm.func @table_process.__obelisk_table_body
// CHECK-SAME: -> i32
// CHECK-NOT: llvm.intr.coro
// CHECK: llvm.switch
// CHECK-NOT: llvm.func @table_process.__obelisk_native_execute
