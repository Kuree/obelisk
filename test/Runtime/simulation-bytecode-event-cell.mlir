// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: %t.exe --execution-tier=native | FileCheck %s

// Event cells retain canonical synchronization-object identities in ordinary
// 64-bit state storage. Loading expands that identity back into an event
// descriptor, while storing and reloading null preserves its no-op behavior.
// This executes the MLIR-originated image, rather than merely checking that it
// serialized.
// CHECK: event cell executed
// CHECK-NOT: null event woke

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @bytecode_event_cell {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9940000 in 0 root_initializer
        hierarchy "top.root"
    simulation.code_unit.decl 9940001 in 0 initial hierarchy "top.initial"
    simulation.code_unit.decl 9940002 in 0 initial hierarchy "top.null_wait"
    simulation.storage.decl 0 in 0 : !simulation.event design
        hierarchy "top.cell"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9940000 : i64} {
      %cell = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.event>
      %process = simulation.spawn @initial(%ctx, %cell) :
          !simulation.context, !simulation.ref<!simulation.event> ->
          !simulation.process
      %waiter = simulation.spawn @null_wait(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %cell: !simulation.ref<!simulation.event>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940001 : i64} {
      %created = simulation.event.create
      simulation.ref.store %created to %cell :
          !simulation.event, !simulation.ref<!simulation.event>
      %loaded = simulation.ref.load %cell :
          !simulation.ref<!simulation.event> -> !simulation.event
      simulation.event.trigger %loaded nonblocking = false

      %null = simulation.event.null
      simulation.ref.store %null to %cell :
          !simulation.event, !simulation.ref<!simulation.event>
      %loaded_null = simulation.ref.load %cell :
          !simulation.ref<!simulation.event> -> !simulation.event
      %yield = simulation.time.constant 0
      simulation.suspend.delay %yield to ^after_yield(
          %loaded_null : !simulation.event)

    ^after_yield(%restored_null: !simulation.event):
      // Carry null through the canonical coroutine frame representation too.
      simulation.event.trigger %restored_null nonblocking = true

      %message = simulation.bytes.constant "event cell executed"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    // A null event is a legal wait operand that can never wake. The bytecode
    // handle-ID path must retain its reserved all-ones sentinel rather than
    // rejecting it as an ordinary invalid descriptor.
    simulation.func private @null_wait(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940002 : i64} {
      %null = simulation.event.null
      simulation.suspend.event %null to ^bad

    ^bad:
      %message = simulation.bytes.constant "null event woke"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
