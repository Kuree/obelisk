// RUN: talos-opt %s | talos-opt | FileCheck %s --check-prefix=IR
// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: talos-opt %s --encode-talos-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 1800-2023 6.16.10: atoreal reads only real-constant syntax (5.7.2), so
// it keeps a runtime entry of its own. A $sscanf floating-point field still
// uses the general parser, which accepts a sign and a bare decimal point.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @atoreal {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %text = simulation.string.literal ".5"
      %constant = simulation.string.parse_real_constant %text :
        (!simulation.string) -> f64
      %field = simulation.string.parse_real %text :
        (!simulation.string) -> f64
      simulation.return
    }
  }
}

// IR: simulation.string.parse_real_constant %{{.*}} : (!simulation.string) -> f64
// IR: simulation.string.parse_real %{{.*}} : (!simulation.string) -> f64

// CHECK-LABEL: llvm.func @process(
// CHECK: llvm.call @talos_rt_v1_string_parse_real_constant(
// CHECK: llvm.call @talos_rt_v1_string_parse_real(
