// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// A known 65-bit four-state index is compacted to a two-state bytecode
// register. The overflow-check round trip must use the same representation;
// otherwise image validation rejects the mixed one-/two-plane comparison.
// The untouched logic element has its normal X default.
// CHECK: 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @wide_two_state_index {
    simulation.scope.decl 0 hierarchy "wide_two_state_index"
    simulation.storage.decl 0 in 0 :
        !simulation.packed_array<7 : 0 x !simulation.logic<1>> design
        hierarchy "wide_two_state_index.value"
    simulation.code_unit.decl 9950000 in 0 root_initializer
        hierarchy "wide_two_state_index.root"
    simulation.code_unit.decl 9950001 in 0 initial
        hierarchy "wide_two_state_index.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9950000 : i64} {
      %array = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
      %process = simulation.spawn @initial(%ctx, %array) :
          !simulation.context,
          !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
          -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %array: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9950001 : i64} {
      %index = simulation.logic.constant 0 : i65, 0 : i65 :
          !simulation.logic<65>
      %element = simulation.ref.array_element %array[%index] :
          (!simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>,
           !simulation.logic<65>) -> !simulation.ref<!simulation.logic<1>>
      %value = simulation.ref.load %element :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %x = simulation.logic.constant false, true : !simulation.logic<1>
      %ok = simulation.logic.compare case_eq %value, %x :
          (!simulation.logic<1>, !simulation.logic<1>) -> i1
      %format = simulation.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %ok)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, i1
      simulation.return
    }
  }
}
