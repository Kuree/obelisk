// RUN: talos-opt %s --talos-sim-materialize-dpi-exports --convert-talos-sim-processes-to-llvm-coroutines -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-translate --mlir-to-llvmir %t.mlir | opt -passes=verify -disable-output
// RUN: talos-opt %s --talos-sim-materialize-dpi-exports --encode-talos-sim-to-bytecode='vpi=off require-bytecode=true' --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s

// The C signature remains svLogicVecVal (two 32-bit fields per word), while
// the internal signature has one i65 value and no unknown payload. Helpers
// receive a null internal unknown pointer and the original C four-state kind.
// CHECK-LABEL: llvm.func @export_vector(
// CHECK-SAME: %{{.*}}: !llvm.ptr, %{{.*}}: !llvm.ptr)
// CHECK: %[[NULL:.*]] = llvm.mlir.zero : !llvm.ptr
// CHECK: %[[LOGIC:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: llvm.call @talos_rt_v1_dpi_export_unpack_vector({{.*}}%[[NULL]], %{{.*}}, %[[LOGIC]])
// CHECK: llvm.call @talos_rt_v1_dpi_export_pack_vector

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.two_state, dpi.has_exports
} {
  simulation.design @exports {
    simulation.scope.decl 0 hierarchy "exports"
    simulation.code_unit.decl 2 in 0 root_initializer hierarchy "exports.root"
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }
    simulation.code_unit.decl 1 in 0 function hierarchy "exports.vector"
    simulation.func @vector(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i65 {simulation.capture_kind = 2 : i32},
        %unused_output: i65 {simulation.capture_kind = 2 : i32}) -> i65
        attributes {
          code_unit_id = 1 : i64, entry_kind = 8 : i32,
          dpi.export, dpi.c_identifier = "export_vector",
          dpi.export_id = 101 : i32, dpi.scope_id = 0 : i64,
          dpi.logical_inputs = 2 : i32,
          dpi.abi_signature = [
            #dpi.abi<kind = logic_vector, direction = input,
                     width = 65, fourState = false, isSigned = false>,
            #dpi.abi<kind = logic_vector, direction = output,
                     width = 65, fourState = false, isSigned = false>,
            #dpi.abi<kind = logic_vector, direction = output,
                     width = 65, fourState = false, isSigned = false>
          ]
        } {
      simulation.return %value : i65
    }
  }
}
