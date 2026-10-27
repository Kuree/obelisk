// RUN: talos-opt %s --test-talos-print-native-size -o /dev/null 2>&1 | FileCheck %s

// Families fold user-named segments to `*` and numeric suffixes to `N`.
// Distinct global contents are keyed structurally: @b.__talos_frame_fields
// equals @a's, @c's wires the same ops differently, and the two descriptors
// differ only in the address they take.

// CHECK: native-size ops=48 function-ops=7 functions=3 global-ops=30 globals=6
// CHECK-NEXT: function *.__talos_eval_body_N count=2 ops=4
// CHECK-NEXT: function *.__talos_eval_body_N.__talos_two_state_N count=1 ops=3
// CHECK-NEXT: global *.__talos_frame_fields count=3 ops=18 distinct=2 distinct-ignoring-addresses=2 addresses=no
// CHECK-NEXT: global *.__talos_process_descriptor count=2 ops=12 distinct=2 distinct-ignoring-addresses=1 addresses=yes
// CHECK-NEXT: global __talos_aot_static_fanout_v1 count=1 ops=0 distinct=1 distinct-ignoring-addresses=1 addresses=no
// CHECK-NOT: {{.}}

module {
  llvm.func @extern(i32)
  llvm.func @top.u_1.proc_3.__talos_eval_body_7(%arg: i32) {
    llvm.call @extern(%arg) : (i32) -> ()
    llvm.return
  }
  llvm.func @p.__talos_eval_body_2(%arg: i32) {
    llvm.call @extern(%arg) : (i32) -> ()
    llvm.return
  }
  llvm.func @p.__talos_eval_body_2.__talos_two_state_0(%arg: i32) {
    %one = llvm.mlir.constant(1 : i32) : i32
    llvm.call @extern(%one) : (i32) -> ()
    llvm.return
  }
  llvm.mlir.global internal constant @a.__talos_frame_fields() : !llvm.array<2 x i32> {
    %0 = llvm.mlir.undef : !llvm.array<2 x i32>
    %1 = llvm.mlir.constant(1 : i32) : i32
    %2 = llvm.mlir.constant(2 : i32) : i32
    %3 = llvm.insertvalue %1, %0[0] : !llvm.array<2 x i32>
    %4 = llvm.insertvalue %2, %3[1] : !llvm.array<2 x i32>
    llvm.return %4 : !llvm.array<2 x i32>
  }
  llvm.mlir.global internal constant @b.__talos_frame_fields() : !llvm.array<2 x i32> {
    %0 = llvm.mlir.undef : !llvm.array<2 x i32>
    %1 = llvm.mlir.constant(1 : i32) : i32
    %2 = llvm.mlir.constant(2 : i32) : i32
    %3 = llvm.insertvalue %1, %0[0] : !llvm.array<2 x i32>
    %4 = llvm.insertvalue %2, %3[1] : !llvm.array<2 x i32>
    llvm.return %4 : !llvm.array<2 x i32>
  }
  llvm.mlir.global internal constant @c.__talos_frame_fields() : !llvm.array<2 x i32> {
    %0 = llvm.mlir.undef : !llvm.array<2 x i32>
    %1 = llvm.mlir.constant(1 : i32) : i32
    %2 = llvm.mlir.constant(2 : i32) : i32
    %3 = llvm.insertvalue %2, %0[0] : !llvm.array<2 x i32>
    %4 = llvm.insertvalue %1, %3[1] : !llvm.array<2 x i32>
    llvm.return %4 : !llvm.array<2 x i32>
  }
  llvm.mlir.global internal constant @a.__talos_process_descriptor() : !llvm.struct<(ptr, i32)> {
    %0 = llvm.mlir.undef : !llvm.struct<(ptr, i32)>
    %1 = llvm.mlir.addressof @a.__talos_frame_fields : !llvm.ptr
    %2 = llvm.mlir.constant(4 : i32) : i32
    %3 = llvm.insertvalue %1, %0[0] : !llvm.struct<(ptr, i32)>
    %4 = llvm.insertvalue %2, %3[1] : !llvm.struct<(ptr, i32)>
    llvm.return %4 : !llvm.struct<(ptr, i32)>
  }
  llvm.mlir.global internal constant @b.__talos_process_descriptor() : !llvm.struct<(ptr, i32)> {
    %0 = llvm.mlir.undef : !llvm.struct<(ptr, i32)>
    %1 = llvm.mlir.addressof @b.__talos_frame_fields : !llvm.ptr
    %2 = llvm.mlir.constant(4 : i32) : i32
    %3 = llvm.insertvalue %1, %0[0] : !llvm.struct<(ptr, i32)>
    %4 = llvm.insertvalue %2, %3[1] : !llvm.struct<(ptr, i32)>
    llvm.return %4 : !llvm.struct<(ptr, i32)>
  }
  llvm.mlir.global internal constant @__talos_aot_static_fanout_v1(dense<[1, 2]> : tensor<2xi32>) : !llvm.array<2 x i32>
}
