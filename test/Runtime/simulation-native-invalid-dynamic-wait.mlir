// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
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
  obelisk_sim.design @invalid_dynamic_wait {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.unpacked_array<15 : 0 x !obelisk_sim.logic<1>> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>> design
    obelisk_sim.code_unit.decl 9951000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9951001 in 0 initial hierarchy "top.waiter"
    obelisk_sim.code_unit.decl 9951002 in 0 initial hierarchy "top.setter"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9951000 : i64} {
      %array = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.unpacked_array<15 : 0 x !obelisk_sim.logic<1>>>
      %selector = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>>
      %x = obelisk_sim.logic.constant 31 : i5, 31 : i5 : !obelisk_sim.logic<5>
      %packed_x = obelisk_sim.packed.unflatten %x :
          (!obelisk_sim.logic<5>) -> !obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>
      obelisk_sim.ref.store %packed_x to %selector :
          !obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>>
      %waiter = obelisk_sim.spawn @waiter(%ctx, %array, %selector) :
          !obelisk_sim.context,
          !obelisk_sim.ref<!obelisk_sim.unpacked_array<15 : 0 x !obelisk_sim.logic<1>>>,
          !obelisk_sim.ref<!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.process
      %setter = obelisk_sim.spawn @setter(%ctx, %selector) :
          !obelisk_sim.context,
          !obelisk_sim.ref<!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>> ->
          !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @waiter(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %array: !obelisk_sim.ref<!obelisk_sim.unpacked_array<15 : 0 x !obelisk_sim.logic<1>>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %selector: !obelisk_sim.ref<!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9951001 : i64} {
      %selected = obelisk_sim.ref.load %selector :
          !obelisk_sim.ref<!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>> ->
          !obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>
      %flat = obelisk_sim.packed.flatten %selected :
          (!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>) ->
          !obelisk_sim.logic<5>
      %index = obelisk_sim.logic.resize %flat signed = false :
          !obelisk_sim.logic<5> -> !obelisk_sim.logic<65>
      %element = obelisk_sim.ref.array_element %array[%index] :
          (!obelisk_sim.ref<!obelisk_sim.unpacked_array<15 : 0 x !obelisk_sim.logic<1>>>,
           !obelisk_sim.logic<65>) -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.suspend.any %selector, %element edges [0, 0] to ^resume :
          !obelisk_sim.ref<!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %stdout = arith.constant 1 : i32
      %message = obelisk_sim.bytes.constant "PASS"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @setter(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %selector: !obelisk_sim.ref<!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9951002 : i64} {
      %tick = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %tick to ^set
    ^set:
      %zero = obelisk_sim.logic.constant 0 : i5, 0 : i5 : !obelisk_sim.logic<5>
      %packed = obelisk_sim.packed.unflatten %zero :
          (!obelisk_sim.logic<5>) -> !obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>
      obelisk_sim.ref.store %packed to %selector :
          !obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.packed_array<4 : 0 x !obelisk_sim.logic<1>>>
      obelisk_sim.return
    }
  }
}
