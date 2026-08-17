// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
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
  obelisk_sim.design @real_overrides {
    obelisk_sim.scope.decl 0 hierarchy "real_overrides"
    obelisk_sim.code_unit.decl 9931000 in 0 root_initializer
        hierarchy "real_overrides.root"
    obelisk_sim.code_unit.decl 9931001 in 0 initial
        hierarchy "real_overrides.initial"
    obelisk_sim.storage.decl 0 in 0 : f64 design
        hierarchy "real_overrides.value"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9931000 : i64} {
      %value = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<f64>
      %process = obelisk_sim.spawn @initial(%ctx, %value) :
          !obelisk_sim.context, !obelisk_sim.ref<f64> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<f64>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9931001 : i64} {
      %initial = arith.constant 2.000000e+00 : f64
      %held = arith.constant 1.250000e+00 : f64
      %blocked = arith.constant 3.000000e+00 : f64
      %changed = arith.constant 4.000000e+00 : f64
      obelisk_sim.ref.store %initial to %value : f64, !obelisk_sim.ref<f64>
      obelisk_sim.override %value = %held assign true :
          !obelisk_sim.ref<f64>, f64
      obelisk_sim.ref.store %blocked to %value : f64, !obelisk_sim.ref<f64>
      %during = obelisk_sim.ref.load %value : !obelisk_sim.ref<f64> -> f64
      %during_ok = arith.cmpf oeq, %during, %held : f64
      obelisk_sim.release_override %value assign true : !obelisk_sim.ref<f64>
      %after = obelisk_sim.ref.load %value : !obelisk_sim.ref<f64> -> f64
      %after_ok = arith.cmpf oeq, %after, %held : f64
      obelisk_sim.ref.store %changed to %value : f64, !obelisk_sim.ref<f64>
      %final = obelisk_sim.ref.load %value : !obelisk_sim.ref<f64> -> f64
      %final_ok = arith.cmpf oeq, %final, %changed : f64
      %format = obelisk_sim.bytes.constant "%0d %0d %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(
          %format, %during_ok, %after_ok, %final_ok)
          newline = true radix = 10 flags = [0, 0, 0, 0] :
          !obelisk_sim.bytes, i1, i1, i1
      obelisk_sim.return
    }
  }
}
