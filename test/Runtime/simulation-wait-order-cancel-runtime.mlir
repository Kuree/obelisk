// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s --implicit-check-not=FAIL
// RUN: %t.exe --execution-tier=native | FileCheck %s --implicit-check-not=FAIL

// A disabled descendant must be removed from ordered-event observation before
// either watched event is published.
// CHECK: cancelled wait: PASS

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @wait_order_cancel_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9940000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9940001 in 0 initial hierarchy "top.parent"
    simulation.code_unit.decl 9940002 in 0 fork hierarchy "top.child"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9940000 : i64} {
      %parent = simulation.spawn @parent(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940001 : i64} {
      %a = simulation.event.create
      %b = simulation.event.create
      %child = simulation.spawn @child(%ctx, %a, %b) :
          !simulation.context, !simulation.event, !simulation.event ->
          !simulation.process
      %yield = simulation.time.constant 0
      simulation.suspend.delay %yield to ^disable(
          %a, %b : !simulation.event, !simulation.event)
    ^disable(%resumed_a: !simulation.event,
             %resumed_b: !simulation.event):
      simulation.children.disable
      simulation.event.trigger %resumed_a nonblocking = false
      simulation.event.trigger %resumed_b nonblocking = false
      %observe = simulation.time.constant 0
      simulation.suspend.delay %observe to ^done
    ^done:
      %message = simulation.bytes.constant "cancelled wait: PASS"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }

    simulation.func private @child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.event {simulation.capture_kind = 1 : i32},
        %b: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9940002 : i64,
                    internal} {
      simulation.suspend.event_order %a, %b events 2 to ^resumed :
          !simulation.event, !simulation.event
    ^resumed:
      %message = simulation.bytes.constant "cancelled wait: FAIL"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
