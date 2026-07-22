// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines --split-input-file --verify-diagnostics

// expected-error @+1 {{coroutine lowering requires an explicit llvm.data_layout}}
module {
}

// -----

// expected-error @+1 {{coroutine lowering currently requires a 64-bit little-endian target}}
module attributes {llvm.data_layout = "E-p:64:64"} {
}

// -----

// expected-error @+1 {{LLVM data layout is incompatible with the Obelisk process ABI}}
module attributes {llvm.data_layout = "e-p:64:64-i64:32"} {
}

// -----

// expected-error @+1 {{llvm.target_triple is inconsistent with the Obelisk process ABI}}
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32",
  llvm.target_triple = "i386-unknown-linux-gnu"
} {
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  obelisk_sim.design @duplicate_continuation {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^second
          {site = #obelisk_sim.continuation<id = 7>}
    ^second:
      // expected-error @+1 {{requires a unique nonzero continuation ID}}
      obelisk_sim.suspend.delay %delay to ^done
          {site = #obelisk_sim.continuation<id = 7>}
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

// The process structs remain compatible, but the wider runtime ABI does not.
// expected-error @+1 {{LLVM data layout is incompatible with the Obelisk runtime ABI}}
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:32-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // expected-error @+1 {{owned runtime buffers cannot be function arguments}}
  func.func @invalid_owned_buffer(%buffer: !obelisk_rt.buffer) {
    return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @native_pointer_capture {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{cannot place type '!obelisk_sim.context' in the canonical process frame}}
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %pointer: !obelisk_sim.context
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}
