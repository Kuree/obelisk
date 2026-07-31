// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @dpi {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "dpi.add" {
      obelisk_sim.dpi_abi_signature = [
        #obelisk_sim.dpi_abi<kind = int, direction = input, width = 32,
                              fourState = false, isSigned = true>,
        #obelisk_sim.dpi_abi<kind = int, direction = result, width = 32,
                              fourState = false, isSigned = true>
      ],
      obelisk_sim.dpi_c_identifier = "c_add",
      obelisk_sim.dpi_import,
      obelisk_sim.dpi_import_id = 17 : i32,
      obelisk_sim.dpi_logical_inputs = 1 : i32
    }
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "dpi.call"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "dpi.notify" {
      obelisk_sim.dpi_abi_signature = [
        #obelisk_sim.dpi_abi<kind = int, direction = input, width = 32,
                              fourState = false, isSigned = true>
      ],
      obelisk_sim.dpi_c_identifier = "notify",
      obelisk_sim.dpi_import,
      obelisk_sim.dpi_import_id = 18 : i32,
      obelisk_sim.dpi_logical_inputs = 1 : i32
    }

    obelisk_sim.func @call(
        %context: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 7 : i32
      %result:2 = obelisk_sim.dpi.call "c_add" id 17 scope 0
          context %context : !obelisk_sim.context(%value) {
            abi_signature = [
              #obelisk_sim.dpi_abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>,
              #obelisk_sim.dpi_abi<kind = int, direction = result, width = 32,
                                    fourState = false, isSigned = true>
            ],
            is_context = false,
            is_pure = true,
            is_task = false,
            source_column = 3 : i32,
            source_file = "dpi.mlir",
            source_line = 12 : i32
          } : (i32) -> (i32, !obelisk_rt.status)
      %void_status = obelisk_sim.dpi.call "notify" id 18 scope 0
          context %context : !obelisk_sim.context(%value) {
            abi_signature = [
              #obelisk_sim.dpi_abi<kind = int, direction = input, width = 32,
                                    fourState = false, isSigned = true>
            ],
            is_context = false,
            is_pure = false,
            is_task = false,
            source_column = 4 : i32,
            source_file = "dpi.mlir",
            source_line = 13 : i32
          } : (i32) -> !obelisk_rt.status
      obelisk_sim.return
    }
  }
}

// CHECK-DAG: llvm.func @notify(i32)
// CHECK-DAG: llvm.func @obelisk_rt_v1_import_call
// CHECK-LABEL: llvm.func internal @__obelisk_dpi_thunk_18(
// CHECK: llvm.call @notify
// CHECK: llvm.return
// CHECK: llvm.func @c_add(i32) -> i32
// CHECK-LABEL: llvm.func internal @__obelisk_dpi_thunk_17(
// CHECK: llvm.call @c_add
// CHECK: llvm.return

// CHECK-LABEL: llvm.func @call(
// CHECK: llvm.call @obelisk_rt_v1_import_call
// CHECK-NOT: obelisk_sim.dpi.call
