// RUN: obelisk-opt %s --split-input-file --address-native-state-through-bases --verify-diagnostics

// Existing bases may be reused only with the exact immutable plane-address
// contract required by the separate physical storage proof.

// -----
// Wrong type.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // expected-error@+1 {{native state base must be a private externally initialized aligned constant pointer initialized to its plane address}}
  llvm.mlir.global internal constant @__obelisk_state_value_base(0 : i64) {alignment = 8 : i64, externally_initialized} : i64
}

// -----
// Program-writable base.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // expected-error@+1 {{native state base must be a private externally initialized aligned constant pointer initialized to its plane address}}
  llvm.mlir.global internal @__obelisk_state_value_base() {alignment = 8 : i64, externally_initialized} : !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
}

// -----
// Wrong plane allocation.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // expected-error@+1 {{native state base must be a private externally initialized aligned constant pointer initialized to its plane address}}
  llvm.mlir.global internal constant @__obelisk_state_value_base() {alignment = 8 : i64, externally_initialized} : !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
}

// -----
// Null initializer.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // expected-error@+1 {{native state base must be a private externally initialized aligned constant pointer initialized to its plane address}}
  llvm.mlir.global internal constant @__obelisk_state_value_base() {alignment = 8 : i64, externally_initialized} : !llvm.ptr {
    %p = llvm.mlir.zero : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
}

// -----
// Offset initializer.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // expected-error@+1 {{native state base must be a private externally initialized aligned constant pointer initialized to its plane address}}
  llvm.mlir.global internal constant @__obelisk_state_value_base() {alignment = 8 : i64, externally_initialized} : !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %q = llvm.getelementptr %p[1] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.return %q : !llvm.ptr
  }
}

// -----
// Foldable base.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // expected-error@+1 {{native state base must be a private externally initialized aligned constant pointer initialized to its plane address}}
  llvm.mlir.global internal constant @__obelisk_state_value_base() {alignment = 8 : i64} : !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
}

// -----
// External base.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // expected-error@+1 {{native state base must be a private externally initialized aligned constant pointer initialized to its plane address}}
  llvm.mlir.global external constant @__obelisk_state_value_base() {alignment = 8 : i64, externally_initialized} : !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
}

// -----
// Insufficient alignment.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // expected-error@+1 {{native state base must be a private externally initialized aligned constant pointer initialized to its plane address}}
  llvm.mlir.global internal constant @__obelisk_state_value_base() {alignment = 1 : i64, externally_initialized} : !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
}

// -----
// Wrong storage address space.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // expected-error@+1 {{native state base must be a private externally initialized aligned constant pointer initialized to its plane address}}
  llvm.mlir.global internal constant @__obelisk_state_value_base() {addr_space = 1 : i32, alignment = 8 : i64, externally_initialized} : !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
}

// -----
// expected-error@+2 {{native state base symbol is not a global}}
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.func @__obelisk_state_value_base()
}
