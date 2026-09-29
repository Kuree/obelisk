// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2017 9.4.2: changing the index expression of an implicit event
// expression re-evaluates a dynamic selection.  While the index is X, the
// selected element has no valid stable handle; the selector remains a valid
// watcher and must wake the process once it becomes known.
// CHECK: PASS

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @invalid_dynamic_wait {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : !simulation.unpacked_array<15 : 0 x !simulation.logic<1>> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<4 : 0 x !simulation.logic<1>> design
    simulation.code_unit.decl 9951000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9951001 in 0 initial hierarchy "top.waiter"
    simulation.code_unit.decl 9951002 in 0 initial hierarchy "top.setter"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9951000 : i64} {
      %array = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.unpacked_array<15 : 0 x !simulation.logic<1>>>
      %selector = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.packed_array<4 : 0 x !simulation.logic<1>>>
      %x = simulation.logic.constant 31 : i5, 31 : i5 : !simulation.logic<5>
      %packed_x = simulation.packed.unflatten %x :
          (!simulation.logic<5>) -> !simulation.packed_array<4 : 0 x !simulation.logic<1>>
      simulation.ref.store %packed_x to %selector :
          !simulation.packed_array<4 : 0 x !simulation.logic<1>>,
          !simulation.ref<!simulation.packed_array<4 : 0 x !simulation.logic<1>>>
      %waiter = simulation.spawn @waiter(%ctx, %array, %selector) :
          !simulation.context,
          !simulation.ref<!simulation.unpacked_array<15 : 0 x !simulation.logic<1>>>,
          !simulation.ref<!simulation.packed_array<4 : 0 x !simulation.logic<1>>> -> !simulation.process
      %setter = simulation.spawn @setter(%ctx, %selector) :
          !simulation.context,
          !simulation.ref<!simulation.packed_array<4 : 0 x !simulation.logic<1>>> ->
          !simulation.process
      simulation.return
    }

    simulation.func private @waiter(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %array: !simulation.ref<!simulation.unpacked_array<15 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %selector: !simulation.ref<!simulation.packed_array<4 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9951001 : i64} {
      %selected = simulation.ref.load %selector :
          !simulation.ref<!simulation.packed_array<4 : 0 x !simulation.logic<1>>> ->
          !simulation.packed_array<4 : 0 x !simulation.logic<1>>
      %flat = simulation.packed.flatten %selected :
          (!simulation.packed_array<4 : 0 x !simulation.logic<1>>) ->
          !simulation.logic<5>
      %index = simulation.logic.resize %flat signed = false :
          !simulation.logic<5> -> !simulation.logic<65>
      %element = simulation.ref.array_element %array[%index] :
          (!simulation.ref<!simulation.unpacked_array<15 : 0 x !simulation.logic<1>>>,
           !simulation.logic<65>) -> !simulation.ref<!simulation.logic<1>>
      simulation.suspend.any %selector, %element edges [0, 0] to ^resume :
          !simulation.ref<!simulation.packed_array<4 : 0 x !simulation.logic<1>>>,
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %stdout = arith.constant 1 : i32
      %message = simulation.bytes.constant "PASS"
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @setter(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %selector: !simulation.ref<!simulation.packed_array<4 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9951002 : i64} {
      %tick = simulation.time.constant 1
      simulation.suspend.delay %tick to ^set
    ^set:
      %zero = simulation.logic.constant 0 : i5, 0 : i5 : !simulation.logic<5>
      %packed = simulation.packed.unflatten %zero :
          (!simulation.logic<5>) -> !simulation.packed_array<4 : 0 x !simulation.logic<1>>
      simulation.ref.store %packed to %selector :
          !simulation.packed_array<4 : 0 x !simulation.logic<1>>,
          !simulation.ref<!simulation.packed_array<4 : 0 x !simulation.logic<1>>>
      simulation.return
    }
  }
}
