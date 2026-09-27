// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines --split-input-file --verify-diagnostics

// expected-error @+1 {{coroutine lowering requires an explicit llvm.data_layout}}
module {
}

// -----

// expected-error @+1 {{coroutine lowering requires a little-endian target with 32-bit or 64-bit pointers}}
module attributes {llvm.data_layout = "E-p:64:64"} {
}

// -----

// expected-error @+1 {{LLVM data layout is incompatible with the Obelisk runtime ABI for i64}}
module attributes {llvm.data_layout = "e-p:64:64-i64:32"} {
}

// -----

// expected-error @+1 {{llvm.target_triple is inconsistent with the supported little-endian 32-bit and 64-bit runtime ABIs}}
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32",
  llvm.target_triple = "i386-unknown-linux-gnu"
} {
}

// -----

!conflicting_path_holder = !simulation.unpacked_struct<[
  #simulation.field<name = "path", type = !simulation.reference_path<i64>, ordinal = 0, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  llvm.mlir.global internal constant @__obelisk_element_trace_77("wrong") {alignment = 1 : i64}
  simulation.design @conflicting_native_trace {
    simulation.scope.decl 0
    simulation.code_unit.decl 77 in 0 function hierarchy "trace"
    simulation.func @trace(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 77 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i64
      // expected-error @+1 {{native byte global @__obelisk_element_trace_77 conflicts with a pre-existing symbol}}
      %values = simulation.container.create %one {
        type_id = 77 : i64, element_kind = #simulation.element_kind<aggregate>,
        element_flags = #simulation.element_flags<none>, value_size = 8 : i64,
        alignment = 8 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64: 0>, trace_kinds = array<i32: 4>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<!conflicting_path_holder>
      simulation.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  simulation.design @duplicate_continuation {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.duplicate_continuation.process.9000001"
    simulation.scope.decl 0
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^second
          {site = #schedule.continuation<id = 7>}
    ^second:
      // expected-error @+1 {{continuation ID names multiple successor blocks}}
      simulation.suspend.delay %delay to ^done
          {site = #schedule.continuation<id = 7>}
    ^done:
      simulation.return
    }
  }
}

// -----

// The process structs are derived from the data layout, but the scalar runtime
// ABI still requires the C ABI's i16 size and alignment.
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
  func.func @invalid_owned_buffer(%buffer: !runtime.buffer) {
    return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @missing_interface_override {
    simulation.scope.decl 0 hierarchy "top"
    simulation.class.decl @Runner id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @AbstractBase id 2 implements [@Runner] {
      is_abstract = true, is_final = false, is_interface = false
    }
    // expected-error @+1 {{concrete class does not implement interface Runner}}
    simulation.class.decl @Concrete id 3 extends @AbstractBase {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.method @Runner_run of @Runner slot 4294967295
        signature_id 17 interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@Runner>) -> i32 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @AbstractBase_run of @AbstractBase slot 0
        signature_id 17 :
      (!simulation.context, !simulation.class_handle<@AbstractBase>) -> i32 {
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
  simulation.design @mixed_automatic_reference_origins {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.mixed_automatic_reference_origins.merge.9000001"
    simulation.scope.decl 0
    simulation.func @merge(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %choose: i1 {simulation.capture_kind = 2 : i32},
        %value: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      // expected-error @+1 {{automatic reference block argument merges distinct ownership origins}}
      %first = simulation.ref.alloc %value : i64 -> !simulation.ref<i64>
      cf.cond_br %choose, ^join(%first : !simulation.ref<i64>), ^other
    ^other:
      %second = simulation.ref.alloc %value : i64 -> !simulation.ref<i64>
      cf.br ^join(%second : !simulation.ref<i64>)
    ^join(%selected: !simulation.ref<i64>):
      %loaded = simulation.ref.load %selected : !simulation.ref<i64> -> i64
      simulation.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @native_pointer_capture {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.native_pointer_capture.process.9000001"
    simulation.scope.decl 0
    // expected-error @+1 {{cannot place type '!simulation.context' in the canonical process frame}}
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %pointer: !simulation.context
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}
