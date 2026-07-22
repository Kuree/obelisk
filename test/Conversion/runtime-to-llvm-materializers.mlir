// RUN: obelisk-opt --convert-obelisk-runtime-to-llvm %s | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  func.func @materializers(%status: !obelisk_rt.status, %bits: i32,
      %value: i13, %unknown: i13, %count: i64) -> (i13, i13, i1, i32) {
    %bytes = obelisk_rt.bytes.constant "abc"
    %size = obelisk_rt.bytes.size %bytes : (!obelisk_rt.bytes) -> i64
    %low = obelisk_rt.bytes.to_packed %bytes, %count
        {high_alignment = false} : (!obelisk_rt.bytes, i64) -> i13
    %scratch = obelisk_rt.bytes.scratch 2
    %high = obelisk_rt.bytes.to_packed %scratch, %count
        {high_alignment = true} : (!obelisk_rt.mut_bytes, i64) -> i13
    %packed_arg = obelisk_rt.argument.packed %value, %unknown
        {is_signed = true} : (i13, i13) -> !obelisk_rt.arg
    %empty_arg = obelisk_rt.argument.empty : () -> !obelisk_rt.arg
    %bytes_arg = obelisk_rt.argument.bytes %bytes
        {is_format_string = true} : (!obelisk_rt.bytes) -> !obelisk_rt.arg
    %args = obelisk_rt.argument.array %packed_arg, %empty_arg, %bytes_arg :
        (!obelisk_rt.arg, !obelisk_rt.arg, !obelisk_rt.arg) ->
        !obelisk_rt.args
    %env = obelisk_rt.format.environment {
      scope = "top", library_cell = "work.top", time_width = 4 : i32,
      time_suffix = "ns", time_multiplier = 1000 : i64
    }
    %fd = obelisk_rt.file_descriptor.from_bits %bits :
        (i32) -> !obelisk_rt.fd
    %roundtrip = obelisk_rt.file_descriptor.to_bits %fd :
        (!obelisk_rt.fd) -> i32
    %status_bits = obelisk_rt.status.to_bits %status :
        (!obelisk_rt.status) -> i32
    %roundtrip_status = obelisk_rt.status.from_bits %status_bits :
        (i32) -> !obelisk_rt.status
    %ok = obelisk_rt.status.is %roundtrip_status, 0
    return %low, %high, %ok, %roundtrip : i13, i13, i1, i32
  }

  func.func @loop_scratch(%again: i1, %count: i64) -> i13 {
    cf.br ^loop
  ^loop:
    %scratch = obelisk_rt.bytes.scratch 2
    %packed = obelisk_rt.bytes.to_packed %scratch, %count
        {high_alignment = false} : (!obelisk_rt.mut_bytes, i64) -> i13
    cf.cond_br %again, ^loop, ^exit(%packed : i13)
  ^exit(%result: i13):
    return %result : i13
  }

  func.func @edge_materializers(%wide: i80) {
    %empty_bytes = obelisk_rt.bytes.constant ""
    %empty_bytes_arg = obelisk_rt.argument.bytes %empty_bytes
        {is_format_string = false} : (!obelisk_rt.bytes) -> !obelisk_rt.arg
    %wide_arg = obelisk_rt.argument.packed %wide
        {is_signed = false} : (i80) -> !obelisk_rt.arg
    %empty_args = obelisk_rt.argument.array : () -> !obelisk_rt.args
    %args = obelisk_rt.argument.array %empty_bytes_arg, %wide_arg :
        (!obelisk_rt.arg, !obelisk_rt.arg) -> !obelisk_rt.args
    return
  }
}

// CHECK-DAG: llvm.mlir.global internal constant @{{.*}}("abc")
// CHECK-DAG: llvm.mlir.global internal constant @{{.*}}("top")
// CHECK-DAG: llvm.mlir.global internal constant @{{.*}}("work.top")
// CHECK-DAG: llvm.mlir.global internal constant @{{.*}}("ns")
// CHECK-LABEL: func.func @materializers(
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.struct<(ptr, i64, ptr, i64, i32, i32, ptr, i64, i64)> {alignment = 8 : i64}
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.struct<(i32, i32, i64, ptr, ptr)> {alignment = 8 : i64}
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.array<2 x i8> {alignment = 1 : i64}
// CHECK: llvm.icmp "ule" {{.*}} : i64
// CHECK: llvm.select
// CHECK: llvm.icmp "ule" {{.*}} : i64
// CHECK: llvm.select
// CHECK: "llvm.intr.memcpy"
// CHECK: llvm.shl
// CHECK: llvm.trunc {{.*}} : i16 to i13
// CHECK: "llvm.intr.memcpy"
// CHECK: llvm.mlir.constant(8 : i16) : i16
// CHECK: llvm.trunc {{.*}} : i16 to i13
// CHECK: llvm.zext {{.*}} : i13 to i64
// CHECK: llvm.store {{.*}} {alignment = 8 : i64} : i64, !llvm.ptr
// CHECK: llvm.insertvalue {{.*}}[4] : !llvm.struct<(ptr, i64, ptr, i64, i32, i32, ptr, i64, i64)>
// CHECK: llvm.mlir.constant(1000 : i64) : i64
// CHECK: llvm.insertvalue {{.*}}[8] : !llvm.struct<(ptr, i64, ptr, i64, i32, i32, ptr, i64, i64)>
// CHECK: llvm.icmp "eq" {{.*}} : i32
// CHECK: return {{.*}} : i13, i13, i1, i32

// CHECK-LABEL: func.func @loop_scratch(
// CHECK: llvm.alloca
// CHECK: cf.br ^bb1
// CHECK: ^bb1:
// CHECK: llvm.mlir.zero : !llvm.array<2 x i8>
// CHECK-NEXT: llvm.store
// CHECK: "llvm.intr.memcpy"

// CHECK-LABEL: func.func @edge_materializers(
// CHECK: llvm.alloca {{.*}} x i128
// CHECK: llvm.mlir.zero : !llvm.ptr
// CHECK: llvm.zext {{.*}} : i80 to i128
// CHECK: llvm.mlir.zero : !llvm.ptr
// CHECK: llvm.mlir.constant(0 : i64) : i64
// CHECK: return
