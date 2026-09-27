// RUN: obelisk-opt --prepare-runtime-to-llvm %s | FileCheck %s --check-prefix=PREPARE
// RUN: obelisk-opt --prepare-runtime-to-llvm %s | obelisk-opt --convert-runtime-to-llvm | FileCheck %s --check-prefix=LOWER
// RUN: obelisk-opt --convert-obelisk-runtime-to-llvm %s > %t.composed
// RUN: obelisk-opt --prepare-runtime-to-llvm --convert-runtime-to-llvm %s > %t.split
// RUN: diff %t.composed %t.split
// RUN: obelisk-opt --prepare-runtime-to-llvm --prepare-runtime-to-llvm --convert-runtime-to-llvm %s > %t.twice
// RUN: diff %t.split %t.twice
// RUN: not obelisk-opt --convert-runtime-to-llvm %s 2>&1 | FileCheck %s --check-prefix=UNPREPARED

module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"} {
  func.func @read(%ctx: !runtime.context, %fd: !runtime.fd) -> i1 {
    %bytes = runtime.bytes.constant "abc"
    %status, %count = runtime.file.write %ctx, %fd, %bytes :
        (!runtime.context, !runtime.fd, !runtime.bytes) -> (!runtime.status, i64)
    %failed = runtime.status.is %status, <io_error>
    return %failed : i1
  }
}
// PREPARE: runtime.llvm_prepared
// PREPARE: runtime.bytes.constant "abc" {runtime.llvm_byte_globals = ["__obelisk_rt_bytes.0"]}
// PREPARE: runtime.file.write
// PREPARE: runtime.status.is %{{.*}}, <io_error>
// LOWER-NOT: runtime.llvm_prepared
// LOWER: llvm.mlir.global internal constant @__obelisk_rt_bytes.0("abc")
// LOWER: llvm.call @obelisk_rt_v1_file_write
// LOWER: llvm.mlir.constant(4 : i32)
// LOWER: llvm.icmp "eq"
// UNPREPARED: requires prepare-runtime-to-llvm before conversion
