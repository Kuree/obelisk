// RUN: talos-opt %s | talos-opt | FileCheck %s
// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: talos-opt %s '--encode-talos-sim-to-bytecode=vpi=full' -o /dev/null
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @boxed {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // CHECK: simulation.vpi_box_type.decl @integer id 1 : i32
    simulation.vpi_box_type.decl @integer id 1 : i32 {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
    simulation.func @bind(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
                         %array: !simulation.dynamic_array<i32> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // CHECK: simulation.box.bind_type %{{.*}} to @integer : !simulation.dynamic_array<i32>
      simulation.box.bind_type %array to @integer : !simulation.dynamic_array<i32>
      simulation.return
    }
  }
}
// NATIVE: llvm.call @talos_rt_v1_box_bind_type
