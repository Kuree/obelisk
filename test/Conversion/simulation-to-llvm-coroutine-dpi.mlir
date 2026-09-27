// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --allow-unregistered-dialect --mlir-to-llvmir \
// RUN:   | opt -passes=verify -disable-output

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @dpi {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "dpi.add" {
      simulation.dpi_abi_signature = [
        #simulation.dpi_abi<kind = int, direction = input, width = 32,
                              fourState = false, isSigned = true>,
        #simulation.dpi_abi<kind = int, direction = result, width = 32,
                              fourState = false, isSigned = true>
      ],
      simulation.dpi_c_identifier = "c_add",
      simulation.dpi_import,
      simulation.dpi_import_id = 17 : i32,
      simulation.dpi_logical_inputs = 1 : i32
    }
    simulation.code_unit.decl 2 in 0 function hierarchy "dpi.call"
    simulation.code_unit.decl 3 in 0 function hierarchy "dpi.notify" {
      simulation.dpi_abi_signature = [
        #simulation.dpi_abi<kind = int, direction = input, width = 32,
                              fourState = false, isSigned = true>
      ],
      simulation.dpi_c_identifier = "notify",
      simulation.dpi_import,
      simulation.dpi_import_id = 18 : i32,
      simulation.dpi_logical_inputs = 1 : i32
    }
    simulation.code_unit.decl 4 in 0 function hierarchy "dpi.echo" {
      simulation.dpi_abi_signature = [
        #simulation.dpi_abi<kind = string, direction = input, width = 64,
                              fourState = false, isSigned = false>,
        #simulation.dpi_abi<kind = string, direction = result, width = 64,
                              fourState = false, isSigned = false>
      ],
      simulation.dpi_c_identifier = "echo",
      simulation.dpi_import,
      simulation.dpi_import_id = 19 : i32,
      simulation.dpi_logical_inputs = 1 : i32
    }
    simulation.code_unit.decl 5 in 0 function hierarchy "dpi.bounce_handle" {
      simulation.dpi_abi_signature = [
        #simulation.dpi_abi<kind = chandle, direction = input, width = 64,
                              fourState = false, isSigned = false>,
        #simulation.dpi_abi<kind = chandle, direction = result, width = 64,
                              fourState = false, isSigned = false>
      ],
      simulation.dpi_c_identifier = "bounce_handle",
      simulation.dpi_import,
      simulation.dpi_import_id = 20 : i32,
      simulation.dpi_logical_inputs = 1 : i32
    }
    simulation.code_unit.decl 6 in 0 function hierarchy "dpi.mutate" {
      simulation.dpi_abi_signature = [
        #simulation.dpi_abi<kind = string, direction = inout, width = 64,
                              fourState = false, isSigned = false>,
        #simulation.dpi_abi<kind = string, direction = output, width = 64,
                              fourState = false, isSigned = false>
      ],
      simulation.dpi_c_identifier = "mutate",
      simulation.dpi_import,
      simulation.dpi_import_id = 21 : i32,
      simulation.dpi_logical_inputs = 1 : i32
    }
    simulation.code_unit.decl 7 in 0 function hierarchy "dpi.transform" {
      simulation.dpi_abi_signature = [
        #simulation.dpi_abi<kind = real, direction = input, width = 64,
                              fourState = false, isSigned = false>,
        #simulation.dpi_abi<kind = shortreal, direction = input, width = 32,
                              fourState = false, isSigned = false>,
        #simulation.dpi_abi<kind = shortreal, direction = output, width = 32,
                              fourState = false, isSigned = false>,
        #simulation.dpi_abi<kind = real, direction = inout, width = 64,
                              fourState = false, isSigned = false>,
        #simulation.dpi_abi<kind = shortreal, direction = result, width = 32,
                              fourState = false, isSigned = false>,
        #simulation.dpi_abi<kind = shortreal, direction = output, width = 32,
                              fourState = false, isSigned = false>,
        #simulation.dpi_abi<kind = real, direction = output, width = 64,
                              fourState = false, isSigned = false>
      ],
      simulation.dpi_c_identifier = "transform",
      simulation.dpi_import,
      simulation.dpi_import_id = 22 : i32,
      simulation.dpi_logical_inputs = 4 : i32
    }

    simulation.func @call(
        %context: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 7 : i32
      %result:2 = simulation.dpi.call "c_add" id 17 scope 0
          context %context : !simulation.context(%value) {
            abi_signature = [
              #simulation.dpi_abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>,
              #simulation.dpi_abi<kind = int, direction = result, width = 32,
                                    fourState = false, isSigned = true>
            ],
            is_context = false,
            is_pure = true,
            is_task = false,
            source_column = 3 : i32,
            source_file = "dpi.mlir",
            source_line = 12 : i32
          } : (i32) -> (i32, !runtime.status)
      %void_status = simulation.dpi.call "notify" id 18 scope 0
          context %context : !simulation.context(%value) {
            abi_signature = [
              #simulation.dpi_abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>
            ],
            is_context = false,
            is_pure = false,
            is_task = false,
            source_column = 4 : i32,
            source_file = "dpi.mlir",
            source_line = 13 : i32
          } : (i32) -> !runtime.status
      %text = simulation.string.literal "hello"
      %echoed:2 = simulation.dpi.call "echo" id 19 scope 0
          context %context : !simulation.context(%text) {
            abi_signature = [
              #simulation.dpi_abi<kind = string, direction = input,
                                    width = 64, fourState = false,
                                    isSigned = false>,
              #simulation.dpi_abi<kind = string, direction = result,
                                    width = 64, fourState = false,
                                    isSigned = false>
            ],
            is_context = false,
            is_pure = false,
            is_task = false,
            source_column = 5 : i32,
            source_file = "dpi.mlir",
            source_line = 14 : i32
          } : (!simulation.string) -> (!simulation.string, !runtime.status)
      %handle = simulation.chandle.null : !simulation.chandle
      %bounced:2 = simulation.dpi.call "bounce_handle" id 20 scope 0
          context %context : !simulation.context(%handle) {
            abi_signature = [
              #simulation.dpi_abi<kind = chandle, direction = input,
                                    width = 64, fourState = false,
                                    isSigned = false>,
              #simulation.dpi_abi<kind = chandle, direction = result,
                                    width = 64, fourState = false,
                                    isSigned = false>
            ],
            is_context = false,
            is_pure = false,
            is_task = false,
            source_column = 6 : i32,
            source_file = "dpi.mlir",
            source_line = 15 : i32
          } : (!simulation.chandle) -> (!simulation.chandle, !runtime.status)
      %mutated:2 = simulation.dpi.call "mutate" id 21 scope 0
          context %context : !simulation.context(%text) {
            abi_signature = [
              #simulation.dpi_abi<kind = string, direction = inout,
                                    width = 64, fourState = false,
                                    isSigned = false>,
              #simulation.dpi_abi<kind = string, direction = output,
                                    width = 64, fourState = false,
                                    isSigned = false>
            ],
            is_context = false,
            is_pure = false,
            is_task = false,
            source_column = 7 : i32,
            source_file = "dpi.mlir",
            source_line = 16 : i32
          } : (!simulation.string) -> (!simulation.string, !runtime.status)
      %real = arith.constant 2.500000e+00 : f64
      %short = arith.constant 1.500000e+00 : f32
      %short_zero = arith.constant 0.000000e+00 : f32
      %accumulated = arith.constant 4.000000e+00 : f64
      %floating:4 = simulation.dpi.call "transform" id 22 scope 0
          context %context : !simulation.context(
            %real, %short, %short_zero, %accumulated) {
            abi_signature = [
              #simulation.dpi_abi<kind = real, direction = input,
                                    width = 64, fourState = false,
                                    isSigned = false>,
              #simulation.dpi_abi<kind = shortreal, direction = input,
                                    width = 32, fourState = false,
                                    isSigned = false>,
              #simulation.dpi_abi<kind = shortreal, direction = output,
                                    width = 32, fourState = false,
                                    isSigned = false>,
              #simulation.dpi_abi<kind = real, direction = inout,
                                    width = 64, fourState = false,
                                    isSigned = false>,
              #simulation.dpi_abi<kind = shortreal, direction = result,
                                    width = 32, fourState = false,
                                    isSigned = false>,
              #simulation.dpi_abi<kind = shortreal, direction = output,
                                    width = 32, fourState = false,
                                    isSigned = false>,
              #simulation.dpi_abi<kind = real, direction = output,
                                    width = 64, fourState = false,
                                    isSigned = false>
            ],
            is_context = false,
            is_pure = false,
            is_task = false,
            source_column = 8 : i32,
            source_file = "dpi.mlir",
            source_line = 17 : i32
          } : (f64, f32, f32, f64) ->
              (f32, f32, f64, !runtime.status)
      simulation.return
    }
  }
}

// BYTECODE: obelisk.bytecode.image = array<i8:

// IEEE 1800-2017 Annex H.8 passes scalar input values directly and output or
// inout formals by pointer. real/realtime use C double; shortreal uses C float.
// CHECK: llvm.func @transform(f64, f32, !llvm.ptr, !llvm.ptr) -> f32
// CHECK-LABEL: llvm.func internal @__obelisk_dpi_thunk_22(
// CHECK: llvm.load %{{.*}} : !llvm.ptr -> f64
// CHECK: llvm.load %{{.*}} : !llvm.ptr -> f32
// CHECK: llvm.call @transform
// CHECK: llvm.store %{{.*}}, %{{.*}} : f32, !llvm.ptr
// CHECK: llvm.store %{{.*}}, %{{.*}} : f64, !llvm.ptr
// CHECK-LABEL: llvm.func internal @__obelisk_dpi_thunk_21(
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_push
// CHECK: llvm.call @mutate
// CHECK: llvm.call @obelisk_rt_v1_dpi_string_copy
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_pop
// CHECK-LABEL: llvm.func internal @__obelisk_dpi_thunk_20(
// CHECK: llvm.inttoptr
// CHECK: llvm.call @bounce_handle
// CHECK: llvm.ptrtoint
// CHECK: llvm.func @obelisk_rt_v1_dpi_string_copy
// CHECK: llvm.func @obelisk_rt_v1_string_view
// CHECK: llvm.func @echo(!llvm.ptr) -> !llvm.ptr
// CHECK-LABEL: llvm.func internal @__obelisk_dpi_thunk_19(
// CHECK: llvm.call @obelisk_rt_v1_string_view
// CHECK: llvm.call @echo
// CHECK: llvm.call @obelisk_rt_v1_dpi_string_copy
// CHECK-LABEL: llvm.func internal @__obelisk_dpi_thunk_18(
// CHECK: llvm.call @notify
// CHECK: llvm.return
// CHECK: llvm.func @c_add(i32) -> i32
// CHECK-LABEL: llvm.func internal @__obelisk_dpi_thunk_17(
// CHECK: llvm.call @c_add
// CHECK: llvm.return

// CHECK-LABEL: llvm.func @call(
// CHECK: llvm.call @obelisk_rt_v1_import_call
// CHECK-NOT: simulation.dpi.call
