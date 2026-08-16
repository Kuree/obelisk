// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | \
// RUN:   mlir-translate --mlir-to-llvmir | opt -passes=verify -disable-output

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native.optimization_level = 3 : i32,
  obelisk.native.max_inline_ops = 2 : i64
} {
  func.func private @small() {
    return
  }

  llvm.func internal @large() attributes {
    passthrough = ["alwaysinline", "presplitcoroutine"]
  } {
    %zero = llvm.mlir.constant(0 : i32) : i32
    %one = llvm.mlir.constant(1 : i32) : i32
    %sum = llvm.add %zero, %one : i32
    llvm.return
  }
}

// CHECK-LABEL: llvm.func @small()
// CHECK-NOT: no_inline
// CHECK-NOT: optimize_none
// CHECK: llvm.return

// CHECK-LABEL: llvm.func internal @large()
// CHECK-SAME: no_inline
// CHECK-SAME: passthrough = ["presplitcoroutine"]
// CHECK-NOT: optimize_none
// CHECK: llvm.return

// CHECK-NOT: obelisk.native.optimization_level
// CHECK-NOT: obelisk.native.max_inline_ops
