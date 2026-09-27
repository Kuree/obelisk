// RUN: obelisk-opt --convert-obelisk-runtime-to-llvm %s | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  func.func @materializers(%status: !runtime.status, %bits: i32,
      %value: i13, %unknown: i13, %count: i64) -> (i13, i13, i1, i32) {
    %bytes = runtime.bytes.constant "abc"
    %size = runtime.bytes.size %bytes : (!runtime.bytes) -> i64
    %low = runtime.bytes.to_packed %bytes, %count
        {high_alignment = false} : (!runtime.bytes, i64) -> i13
    %scratch = runtime.bytes.scratch 2
    %high = runtime.bytes.to_packed %scratch, %count
        {high_alignment = true} : (!runtime.mut_bytes, i64) -> i13
    %packed_arg = runtime.argument.packed %value, %unknown
        {is_signed = true} : (i13, i13) -> !runtime.arg
    %empty_arg = runtime.argument.empty : () -> !runtime.arg
    %bytes_arg = runtime.argument.bytes %bytes
        {is_format_string = true} : (!runtime.bytes) -> !runtime.arg
    %args = runtime.argument.array %packed_arg, %empty_arg, %bytes_arg :
        (!runtime.arg, !runtime.arg, !runtime.arg) ->
        !runtime.args
    %env = runtime.format.environment {
      scope = "top", library_cell = "work.top", time_width = 4 : i32,
      time_suffix = "ns", time_multiplier = 1000 : i64
    }
    %fd = runtime.file_descriptor.from_bits %bits :
        (i32) -> !runtime.fd
    %roundtrip = runtime.file_descriptor.to_bits %fd :
        (!runtime.fd) -> i32
    %status_bits = runtime.status.to_bits %status :
        (!runtime.status) -> i32
    %roundtrip_status = runtime.status.from_bits %status_bits :
        (i32) -> !runtime.status
    %ok = runtime.status.is %roundtrip_status, <ok>
    return %low, %high, %ok, %roundtrip : i13, i13, i1, i32
  }

  func.func @loop_scratch(%again: i1, %count: i64) -> i13 {
    cf.br ^loop
  ^loop:
    %scratch = runtime.bytes.scratch 2
    %packed = runtime.bytes.to_packed %scratch, %count
        {high_alignment = false} : (!runtime.mut_bytes, i64) -> i13
    cf.cond_br %again, ^loop, ^exit(%packed : i13)
  ^exit(%result: i13):
    return %result : i13
  }

  func.func @edge_materializers(%wide: i80, %count: i64) -> i8 {
    // A byte-wide destination already spans exactly one byte, so assembling it
    // must not widen the loaded byte.
    %byte_scratch = runtime.bytes.scratch 1
    %byte = runtime.bytes.to_packed %byte_scratch, %count
        {high_alignment = false} : (!runtime.mut_bytes, i64) -> i8
    %empty_bytes = runtime.bytes.constant ""
    %empty_bytes_arg = runtime.argument.bytes %empty_bytes
        {is_format_string = false} : (!runtime.bytes) -> !runtime.arg
    %wide_arg = runtime.argument.packed %wide
        {is_signed = false} : (i80) -> !runtime.arg
    %empty_args = runtime.argument.array : () -> !runtime.args
    %args = runtime.argument.array %empty_bytes_arg, %wide_arg :
        (!runtime.arg, !runtime.arg) -> !runtime.args
    return %byte : i8
  }

  // $readmemb and $readmemh produce numeric planes with their least
  // significant byte first, unlike file reads whose bytes retain stream order.
  func.func @least_significant_byte_first(%scratch: !runtime.mut_bytes)
      -> i24 {
    %count = arith.constant 3 : i64
    %packed = runtime.bytes.to_packed %scratch, %count
        {high_alignment = false, least_significant_byte_first = true} :
        (!runtime.mut_bytes, i64) -> i24
    return %packed : i24
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
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.array<1 x i8> {alignment = 1 : i64}
// CHECK-DAG: llvm.alloca {{.*}} x i128
// CHECK: "llvm.intr.memcpy"
// CHECK-NOT: llvm.zext {{.*}} : i8 to i8
// CHECK: llvm.shl {{.*}} : i8
// CHECK: llvm.mlir.zero : !llvm.ptr
// CHECK: llvm.zext {{.*}} : i80 to i128
// CHECK: llvm.mlir.zero : !llvm.ptr
// CHECK: llvm.mlir.constant(0 : i64) : i64
// CHECK: return {{.*}} : i8

// CHECK-LABEL: func.func @least_significant_byte_first
// CHECK: llvm.mlir.constant(0 : i24) : i24
// CHECK: llvm.shl {{.*}}, %{{.*}} : i24
// CHECK: llvm.mlir.constant(8 : i24) : i24
// CHECK: llvm.shl {{.*}}, %{{.*}} : i24
// CHECK: llvm.mlir.constant(16 : i24) : i24
// CHECK: llvm.shl {{.*}}, %{{.*}} : i24
