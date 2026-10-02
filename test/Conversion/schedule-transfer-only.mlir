// RUN: obelisk-opt %s -split-input-file --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// A module made entirely of shared transfers still needs descriptor support.
!ref = !simulation.ref<i8>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"} {
  simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %dst: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
      attributes {entry_kind = 9 : i32} {
    cf.br ^loop
  ^loop:
    %v = simulation.ref.load %src : !ref -> i8
    simulation.ref.store %v to %dst : i8, !ref
    simulation.suspend.change %src to ^loop : !ref
  }
  simulation.func @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
      %dst: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
      attributes {entry_kind = 9 : i32} {
    cf.br ^loop
  ^loop:
    %v = simulation.ref.load %src : !ref -> i8
    simulation.ref.store %v to %dst : i8, !ref
    simulation.suspend.change %src to ^loop : !ref
  }
}
// CHECK-DAG: llvm.func internal @__obelisk_native_noop_destroy_v1
// CHECK-DAG: llvm.func internal @__obelisk_native_zero_requirements_v1
// CHECK-DAG: llvm.func @__obelisk_transfer_kernel_0.impl.__obelisk_table_body

// -----

// A singleton uses its selected table entry without manufacturing a kernel.
!ref = !simulation.ref<i8>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"} {
  simulation.func @singleton(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %dst: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
      attributes {entry_kind = 9 : i32} {
    cf.br ^loop
  ^loop:
    %v = simulation.ref.load %src : !ref -> i8
    simulation.ref.store %v to %dst : i8, !ref
    simulation.suspend.change %src to ^loop : !ref
  }
}
// CHECK-LABEL: llvm.func @singleton.__obelisk_table_body
// CHECK-NOT: llvm.func @__obelisk_transfer_kernel
