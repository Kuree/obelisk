// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// A procedural assignment to a real variable holds its value against ordinary
// stores. Deassign leaves the held value in place and permits later stores.
// Exercise the native runtime, serialized-bytecode validation, and interpreter.
// CHECK: 1 1 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @real_overrides {
    simulation.scope.decl 0 hierarchy "real_overrides"
    simulation.code_unit.decl 9931000 in 0 root_initializer
        hierarchy "real_overrides.root"
    simulation.code_unit.decl 9931001 in 0 initial
        hierarchy "real_overrides.initial"
    simulation.storage.decl 0 in 0 : f64 design
        hierarchy "real_overrides.value"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9931000 : i64} {
      %value = simulation.context.storage %ctx[0] : !simulation.ref<f64>
      %process = simulation.spawn @initial(%ctx, %value) :
          !simulation.context, !simulation.ref<f64> -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<f64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9931001 : i64} {
      %initial = arith.constant 2.000000e+00 : f64
      %held = arith.constant 1.250000e+00 : f64
      %blocked = arith.constant 3.000000e+00 : f64
      %changed = arith.constant 4.000000e+00 : f64
      simulation.ref.store %initial to %value : f64, !simulation.ref<f64>
      simulation.override %value = %held assign true :
          !simulation.ref<f64>, f64
      simulation.ref.store %blocked to %value : f64, !simulation.ref<f64>
      %during = simulation.ref.load %value : !simulation.ref<f64> -> f64
      %during_ok = arith.cmpf oeq, %during, %held : f64
      simulation.release_override %value assign true : !simulation.ref<f64>
      %after = simulation.ref.load %value : !simulation.ref<f64> -> f64
      %after_ok = arith.cmpf oeq, %after, %held : f64
      simulation.ref.store %changed to %value : f64, !simulation.ref<f64>
      %final = simulation.ref.load %value : !simulation.ref<f64> -> f64
      %final_ok = arith.cmpf oeq, %final, %changed : f64
      %format = simulation.bytes.constant "%0d %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(
          %format, %during_ok, %after_ok, %final_ok)
          newline = true radix = <decimal> flags = [0, 0, 0, 0] :
          !simulation.bytes, i1, i1, i1
      simulation.return
    }
  }
}
