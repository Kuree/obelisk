// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// A persistent element reference owns an internal weak link back to its
// reference-path object. Exercise that runtime representation directly and
// verify that writes through a dynamic-array element lvalue work in both
// execution tiers.
// CHECK: 42

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @container_reference_runtime {
    obelisk_sim.scope.decl 0 hierarchy "container_reference_runtime"
    obelisk_sim.code_unit.decl 9950000 in 0 root_initializer
        hierarchy "container_reference_runtime.root"
    obelisk_sim.code_unit.decl 9950001 in 0 initial
        hierarchy "container_reference_runtime.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9950000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9950001 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %value = arith.constant 42 : i64
      %array = obelisk_sim.container.create %one {
        type_id = 9950002 : i64, element_kind = 1 : i32,
        element_flags = 2 : i32, value_size = 8 : i64,
        alignment = 8 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<i64>
      %storage = obelisk_sim.ref.alloc %array :
          !obelisk_sim.dynamic_array<i64> ->
          !obelisk_sim.ref<!obelisk_sim.dynamic_array<i64>>
      %owner = obelisk_sim.argument_ref.from_ref %storage :
          !obelisk_sim.ref<!obelisk_sim.dynamic_array<i64>> ->
          !obelisk_sim.argument_ref<!obelisk_sim.dynamic_array<i64>>
      %path = obelisk_sim.reference_path.index %ctx, %array[%zero]
          watching %owner :
          (!obelisk_sim.context, !obelisk_sim.dynamic_array<i64>, i64,
           !obelisk_sim.argument_ref<!obelisk_sim.dynamic_array<i64>>) ->
          !obelisk_sim.reference_path<i64>
      %reference = obelisk_sim.argument_ref.from_path %path :
          !obelisk_sim.reference_path<i64> -> !obelisk_sim.argument_ref<i64>
      obelisk_sim.argument_ref.store %value to %reference :
          i64, !obelisk_sim.argument_ref<i64>
      %result = obelisk_sim.container.read %array, %zero :
          (!obelisk_sim.dynamic_array<i64>, i64) -> i64
      %format = obelisk_sim.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %result)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, i64
      obelisk_sim.return
    }
  }
}
