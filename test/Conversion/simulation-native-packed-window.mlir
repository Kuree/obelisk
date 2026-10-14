// RUN: obelisk-opt %s --obelisk-sim-build-compute-graph='vpi=off' --obelisk-sim-specialize-static-state-nba --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=WINDOW < %t.mlir

// Unaligned array accesses use one native window, even at the plane tail.
// Logical selectors and masks still bound the operation to its element.
// Runtime byte-level preservation is checked in the paired Runtime test.
module attributes {schedule.native_scheduler = 3 : i32, native.closed_executable, llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @windows {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.unpacked_array<0 : 24 x !simulation.logic<3>> design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 24 x !simulation.logic<33>> design
    simulation.storage.decl 2 in 0 : !simulation.unpacked_array<0 : 24 x !simulation.logic<57>> design
    simulation.storage.decl 3 in 0 : !simulation.unpacked_array<0 : 24 x !simulation.logic<17>> design
    simulation.code_unit.decl 5 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 6 in 0 initial hierarchy "check"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 5 : i64} {
      %p = simulation.spawn @check(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %index = arith.constant 24 : i32
      simulation.call @write3(%ctx, %index) : (!simulation.context, i32) -> ()
      simulation.call @write17(%ctx, %index) : (!simulation.context, i32) -> ()
      simulation.call @write33(%ctx, %index) : (!simulation.context, i32) -> ()
      simulation.call @write57(%ctx, %index) : (!simulation.context, i32) -> ()
      simulation.return
    }
    simulation.code_unit.decl 1 in 0 function hierarchy "write3"
    simulation.func @write3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.unpacked_array<0 : 24 x !simulation.logic<3>>>
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!simulation.unpacked_array<0 : 24 x !simulation.logic<3>>>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.logic.constant 5 : i3, 6 : i3 : !simulation.logic<3>
      simulation.ref.store %value to %element : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      simulation.return
    }
    simulation.code_unit.decl 2 in 0 function hierarchy "write17"
    simulation.func @write17(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.unpacked_array<0 : 24 x !simulation.logic<17>>>
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!simulation.unpacked_array<0 : 24 x !simulation.logic<17>>>, i32) -> !simulation.ref<!simulation.logic<17>>
      %value = simulation.logic.constant 5 : i17, 6 : i17 : !simulation.logic<17>
      simulation.ref.store %value to %element : !simulation.logic<17>, !simulation.ref<!simulation.logic<17>>
      simulation.return
    }
    simulation.code_unit.decl 3 in 0 function hierarchy "write33"
    simulation.func @write33(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 24 x !simulation.logic<33>>>
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!simulation.unpacked_array<0 : 24 x !simulation.logic<33>>>, i32) -> !simulation.ref<!simulation.logic<33>>
      %value = simulation.logic.constant 5 : i33, 6 : i33 : !simulation.logic<33>
      simulation.ref.store %value to %element : !simulation.logic<33>, !simulation.ref<!simulation.logic<33>>
      simulation.return
    }
    simulation.code_unit.decl 4 in 0 function hierarchy "write57"
    simulation.func @write57(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<0 : 24 x !simulation.logic<57>>>
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!simulation.unpacked_array<0 : 24 x !simulation.logic<57>>>, i32) -> !simulation.ref<!simulation.logic<57>>
      %value = simulation.logic.constant 5 : i57, 6 : i57 : !simulation.logic<57>
      simulation.ref.store %value to %element : !simulation.logic<57>, !simulation.ref<!simulation.logic<57>>
      simulation.return
    }
  }
}
// CHECK-LABEL: llvm.func @write3(
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i16
// CHECK: llvm.store {{.*}} : i16, !llvm.ptr
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i16
// CHECK: llvm.store {{.*}} : i16, !llvm.ptr
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @write17(
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i32
// CHECK: llvm.store {{.*}} : i32, !llvm.ptr
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i32
// CHECK: llvm.store {{.*}} : i32, !llvm.ptr
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @write33(
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i64
// CHECK: llvm.store {{.*}} : i64, !llvm.ptr
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i64
// CHECK: llvm.store {{.*}} : i64, !llvm.ptr
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @write57(
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i64
// CHECK: llvm.store {{.*}} : i64, !llvm.ptr
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i64
// CHECK: llvm.store {{.*}} : i64, !llvm.ptr
// CHECK: llvm.return
// WINDOW-LABEL: llvm.func @write3(
// WINDOW-NOT: : !llvm.ptr -> i8
// WINDOW-NOT: : i8, !llvm.ptr
// WINDOW: llvm.return
// WINDOW-LABEL: llvm.func @write17(
// WINDOW-NOT: : !llvm.ptr -> i24
// WINDOW-NOT: : i24, !llvm.ptr
// WINDOW: llvm.return
// WINDOW-LABEL: llvm.func @write33(
// WINDOW-NOT: : !llvm.ptr -> i40
// WINDOW-NOT: : i40, !llvm.ptr
// WINDOW: llvm.return
