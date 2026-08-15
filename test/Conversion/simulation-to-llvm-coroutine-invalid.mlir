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

!conflicting_path_holder = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "path", type = !obelisk_sim.reference_path<i64>, ordinal = 0, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  llvm.mlir.global internal constant @__obelisk_element_trace_77("wrong") {alignment = 1 : i64}
  obelisk_sim.design @conflicting_native_trace {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 77 in 0 function hierarchy "trace"
    obelisk_sim.func @trace(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 77 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i64
      // expected-error @+1 {{native byte global @__obelisk_element_trace_77 conflicts with a pre-existing symbol}}
      %values = obelisk_sim.container.create %one {
        type_id = 77 : i64, element_kind = 7 : i32,
        element_flags = 0 : i32, value_size = 8 : i64,
        alignment = 8 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64: 0>, trace_kinds = array<i32: 4>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<!conflicting_path_holder>
      obelisk_sim.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  obelisk_sim.design @duplicate_continuation {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.duplicate_continuation.process.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^second
          {site = #obelisk_sim.continuation<id = 7>}
    ^second:
      // expected-error @+1 {{continuation ID names multiple successor blocks}}
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
  obelisk_sim.design @missing_interface_override {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.class.decl @Runner id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @AbstractBase id 2 implements [@Runner] {
      is_abstract = true, is_final = false, is_interface = false
    }
    // expected-error @+1 {{concrete class does not implement interface Runner}}
    obelisk_sim.class.decl @Concrete id 3 extends @AbstractBase {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.method @Runner_run of @Runner slot 4294967295
        signature_id 17 interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Runner>) -> i32 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @AbstractBase_run of @AbstractBase slot 0
        signature_id 17 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@AbstractBase>) -> i32 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @mixed_automatic_reference_origins {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.mixed_automatic_reference_origins.merge.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @merge(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %choose: i1 {obelisk_sim.capture_kind = 2 : i32},
        %value: i64 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{automatic reference block argument merges distinct ownership origins}}
      %first = obelisk_sim.ref.alloc %value : i64 -> !obelisk_sim.ref<i64>
      cf.cond_br %choose, ^join(%first : !obelisk_sim.ref<i64>), ^other
    ^other:
      %second = obelisk_sim.ref.alloc %value : i64 -> !obelisk_sim.ref<i64>
      cf.br ^join(%second : !obelisk_sim.ref<i64>)
    ^join(%selected: !obelisk_sim.ref<i64>):
      %loaded = obelisk_sim.ref.load %selected : !obelisk_sim.ref<i64> -> i64
      obelisk_sim.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @native_pointer_capture {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.native_pointer_capture.process.9000001"
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{cannot place type '!obelisk_sim.context' in the canonical process frame}}
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %pointer: !obelisk_sim.context
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}
