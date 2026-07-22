// RUN: obelisk-opt --convert-obelisk-runtime-to-llvm --split-input-file --verify-diagnostics %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  llvm.func @obelisk_rt_v1_file_close(i64) -> i64
  func.func @incompatible(%ctx: !obelisk_rt.context,
      %fd: !obelisk_rt.fd) {
    // expected-error @+2 {{runtime symbol @obelisk_rt_v1_file_close is predeclared with an incompatible type}}
    // expected-error @+1 {{failed to legalize operation 'obelisk_rt.file.close'}}
    %status = obelisk_rt.file.close %ctx, %fd :
        (!obelisk_rt.context, !obelisk_rt.fd) -> !obelisk_rt.status
    return
  }
}

// -----

// expected-error @+1 {{runtime lowering requires an explicit llvm.data_layout}}
module {
  func.func private @target(!obelisk_rt.context)
}

// -----

// expected-error @+1 {{runtime lowering currently requires a 64-bit little-endian target}}
module attributes {llvm.data_layout = "e-p:32:32"} {
  func.func private @target(!obelisk_rt.context)
}

// -----

// expected-error @+1 {{runtime lowering currently requires a 64-bit little-endian target}}
module attributes {llvm.data_layout = "E-p:64:64"} {
  func.func private @target(!obelisk_rt.context)
}

// -----

// expected-error @+1 {{LLVM data layout is incompatible with the Obelisk runtime ABI for i64}}
module attributes {llvm.data_layout = "e-p:64:64-i64:32"} {
  func.func private @target(!obelisk_rt.context)
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  llvm.func fastcc @obelisk_rt_v1_file_close(!llvm.ptr, i32) -> i32
  func.func @wrong_calling_convention(%ctx: !obelisk_rt.context,
      %fd: !obelisk_rt.fd) {
    // expected-error @+2 {{runtime symbol @obelisk_rt_v1_file_close must be an external public declaration using the C calling convention}}
    // expected-error @+1 {{failed to legalize operation 'obelisk_rt.file.close'}}
    %status = obelisk_rt.file.close %ctx, %fd :
        (!obelisk_rt.context, !obelisk_rt.fd) -> !obelisk_rt.status
    return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  llvm.func @obelisk_rt_v1_file_close(%ctx: !llvm.ptr, %fd: i32) -> i32 {
    %zero = llvm.mlir.constant(0 : i32) : i32
    llvm.return %zero : i32
  }
  func.func @runtime_symbol_definition(%ctx: !obelisk_rt.context,
      %fd: !obelisk_rt.fd) {
    // expected-error @+2 {{runtime symbol @obelisk_rt_v1_file_close must be an external public declaration using the C calling convention}}
    // expected-error @+1 {{failed to legalize operation 'obelisk_rt.file.close'}}
    %status = obelisk_rt.file.close %ctx, %fd :
        (!obelisk_rt.context, !obelisk_rt.fd) -> !obelisk_rt.status
    return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  llvm.func @obelisk_rt_v1_file_close(!llvm.ptr, i32) -> i32
      attributes {sym_visibility = "private"}
  func.func @private_runtime_symbol(%ctx: !obelisk_rt.context,
      %fd: !obelisk_rt.fd) {
    // expected-error @+2 {{runtime symbol @obelisk_rt_v1_file_close must be an external public declaration using the C calling convention}}
    // expected-error @+1 {{failed to legalize operation 'obelisk_rt.file.close'}}
    %status = obelisk_rt.file.close %ctx, %fd :
        (!obelisk_rt.context, !obelisk_rt.fd) -> !obelisk_rt.status
    return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  // expected-error @+1 {{owned runtime buffers cannot be function results}}
  func.func private @source() -> !obelisk_rt.buffer
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  // expected-error @+1 {{owned runtime buffers cannot be function arguments}}
  func.func @sink(%buffer: !obelisk_rt.buffer) {
    return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  func.func @parallel_call(%ctx: !obelisk_rt.context,
      %fd: !obelisk_rt.fd, %lower: index, %upper: index, %step: index) {
    scf.parallel (%index) = (%lower) to (%upper) step (%step) {
      // expected-error @+2 {{cannot lower a runtime call nested in a concurrent region with function-entry ABI scratch storage}}
      // expected-error @+1 {{failed to legalize operation 'obelisk_rt.file.getc'}}
      %status, %byte = obelisk_rt.file.getc %ctx, %fd :
          (!obelisk_rt.context, !obelisk_rt.fd) -> (!obelisk_rt.status, i8)
      scf.reduce
    }
    return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  func.func @parallel_materializer(%lower: index, %upper: index, %step: index) {
    scf.parallel (%index) = (%lower) to (%upper) step (%step) {
      // expected-error @+2 {{cannot lower a stack-backed runtime materializer nested in a concurrent region with function-entry storage}}
      // expected-error @+1 {{failed to legalize operation 'obelisk_rt.bytes.scratch'}}
      %scratch = obelisk_rt.bytes.scratch 4
      scf.reduce
    }
    return
  }
}

// -----

// expected-error @+1 {{llvm.target_triple is inconsistent with the supported 64-bit little-endian runtime ABI}}
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "powerpc64-unknown-linux-gnu"
} {
  func.func private @target(!obelisk_rt.context)
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  // expected-error @+1 {{attribute 'obelisk_rt.type_marker' contains an obelisk_rt type after runtime-to-LLVM conversion}}
  func.func private @typed_attribute()
      attributes {obelisk_rt.type_marker = !obelisk_rt.context}
}
