// RUN: talos-opt %s --split-input-file --verify-diagnostics

// -----
module {
  llvm.func @invalid(%base: !llvm.ptr, %source: !llvm.ptr) {
    // expected-error@+1 {{ready capacity must be between 1 and UINT32_MAX}}
    schedule.ready.merge %base, %source {capacity = 0 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
}

// -----
module {
  llvm.func @invalid(%base: !llvm.ptr, %source: !llvm.ptr) {
    // expected-error@+1 {{ready capacity must be between 1 and UINT32_MAX}}
    schedule.ready.merge %base, %source {capacity = -1 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
}

// -----
module {
  llvm.func @invalid(%base: !llvm.ptr, %source: !llvm.ptr) {
    // expected-error@+1 {{ready capacity must be between 1 and UINT32_MAX}}
    schedule.ready.merge %base, %source {capacity = 4294967296 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
}
