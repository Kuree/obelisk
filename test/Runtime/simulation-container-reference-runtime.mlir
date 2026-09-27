// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  simulation.design @container_reference_runtime {
    simulation.scope.decl 0 hierarchy "container_reference_runtime"
    simulation.code_unit.decl 9950000 in 0 root_initializer
        hierarchy "container_reference_runtime.root"
    simulation.code_unit.decl 9950001 in 0 initial
        hierarchy "container_reference_runtime.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9950000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9950001 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %value = arith.constant 42 : i64
      %array = simulation.container.create %one {
        type_id = 9950002 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<signed>, value_size = 8 : i64,
        alignment = 8 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<i64>
      %storage = simulation.ref.alloc %array :
          !simulation.dynamic_array<i64> ->
          !simulation.ref<!simulation.dynamic_array<i64>>
      %owner = simulation.argument_ref.from_ref %storage :
          !simulation.ref<!simulation.dynamic_array<i64>> ->
          !simulation.argument_ref<!simulation.dynamic_array<i64>>
      %path = simulation.reference_path.index %ctx, %array[%zero]
          watching %owner :
          (!simulation.context, !simulation.dynamic_array<i64>, i64,
           !simulation.argument_ref<!simulation.dynamic_array<i64>>) ->
          !simulation.reference_path<i64>
      %reference = simulation.argument_ref.from_path %path :
          !simulation.reference_path<i64> -> !simulation.argument_ref<i64>
      simulation.argument_ref.store %value to %reference :
          i64, !simulation.argument_ref<i64>
      %result = simulation.container.read %array, %zero :
          (!simulation.dynamic_array<i64>, i64) -> i64
      %format = simulation.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %result)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, i64
      simulation.return
    }
  }
}
