// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @bytecode_event_cell {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 9940000 in 0 root_initializer
        hierarchy "top.root"
    obelisk_sim.code_unit.decl 9940001 in 0 initial hierarchy "top.initial"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.event design
        hierarchy "top.cell"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9940000 : i64} {
      %cell = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.event>
      %process = obelisk_sim.spawn @initial(%ctx, %cell) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.event> ->
          !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %cell: !obelisk_sim.ref<!obelisk_sim.event>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940001 : i64} {
      %created = obelisk_sim.event.create
      obelisk_sim.ref.store %created to %cell :
          !obelisk_sim.event, !obelisk_sim.ref<!obelisk_sim.event>
      %loaded = obelisk_sim.ref.load %cell :
          !obelisk_sim.ref<!obelisk_sim.event> -> !obelisk_sim.event
      obelisk_sim.event.trigger %loaded nonblocking = false

      %null = obelisk_sim.event.null
      obelisk_sim.ref.store %null to %cell :
          !obelisk_sim.event, !obelisk_sim.ref<!obelisk_sim.event>
      %loaded_null = obelisk_sim.ref.load %cell :
          !obelisk_sim.ref<!obelisk_sim.event> -> !obelisk_sim.event
      %yield = obelisk_sim.time.constant 0
      obelisk_sim.suspend.delay %yield to ^after_yield(
          %loaded_null : !obelisk_sim.event)

    ^after_yield(%restored_null: !obelisk_sim.event):
      // Carry null through the canonical coroutine frame representation too.
      obelisk_sim.event.trigger %restored_null nonblocking = true

      %message = obelisk_sim.bytes.constant "event cell executed"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
