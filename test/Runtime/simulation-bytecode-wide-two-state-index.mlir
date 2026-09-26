// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  obelisk_sim.design @wide_two_state_index {
    obelisk_sim.scope.decl 0 hierarchy "wide_two_state_index"
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>> design
        hierarchy "wide_two_state_index.value"
    obelisk_sim.code_unit.decl 9950000 in 0 root_initializer
        hierarchy "wide_two_state_index.root"
    obelisk_sim.code_unit.decl 9950001 in 0 initial
        hierarchy "wide_two_state_index.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9950000 : i64} {
      %array = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
      %process = obelisk_sim.spawn @initial(%ctx, %array) :
          !obelisk_sim.context,
          !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %array: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9950001 : i64} {
      %index = obelisk_sim.logic.constant 0 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      %element = obelisk_sim.ref.array_element %array[%index] :
          (!obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>,
           !obelisk_sim.logic<65>) -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %value = obelisk_sim.ref.load %element :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %x = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      %ok = obelisk_sim.logic.compare case_eq %value, %x :
          (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      %format = obelisk_sim.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %ok)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, i1
      obelisk_sim.return
    }
  }
}
