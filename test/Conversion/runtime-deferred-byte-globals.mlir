// RUN: obelisk-opt %s --convert-obelisk-runtime-to-llvm -o %t.pending
// RUN: FileCheck %s --check-prefix=PENDING --implicit-check-not='llvm.mlir.global internal constant @__obelisk_rt_bytes' < %t.pending
// RUN: obelisk-opt %t.pending --symbol-dce --materialize-runtime-byte-globals -o %t.live
// RUN: FileCheck %s --check-prefix=LIVE --implicit-check-not='dead literal' --implicit-check-not=schedule.bytes.address < %t.live
// RUN: obelisk-opt %t.live --materialize-runtime-byte-globals -o %t.again
// RUN: diff %t.live %t.again

module attributes {obelisk.native.closed_executable,
                   llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"} {
  // IEEE 1800-2023 5.9: embedded NUL bytes do not truncate the literal.
  func.func @live() -> !runtime.bytes {
    %bytes = runtime.bytes.constant "A\00B"
    return %bytes : !runtime.bytes
  }
  func.func private @unused() -> !runtime.bytes {
    %bytes = runtime.bytes.constant "dead literal"
    return %bytes : !runtime.bytes
  }
  // Native inlining can copy one prepared address into multiple live bodies.
  func.func @copy_one() -> !llvm.ptr {
    %p = schedule.bytes.address "shared" = "same" alignment 1 : !llvm.ptr
    return %p : !llvm.ptr
  }
  func.func @copy_two() -> !llvm.ptr {
    %p = schedule.bytes.address "shared" = "same" alignment 1 : !llvm.ptr
    return %p : !llvm.ptr
  }
}

// PENDING: schedule.bytes.address {{.*}} = "A\00B" alignment 1 : !llvm.ptr
// PENDING: schedule.bytes.address {{.*}} = "dead literal" alignment 1 : !llvm.ptr
// LIVE-COUNT-1: llvm.mlir.global internal constant @shared("same")
// LIVE: llvm.mlir.global internal constant @__obelisk_rt_bytes.0("A\00B")
// LIVE: llvm.mlir.addressof @__obelisk_rt_bytes.0
// LIVE: llvm.mlir.addressof @shared
// LIVE: llvm.mlir.addressof @shared
