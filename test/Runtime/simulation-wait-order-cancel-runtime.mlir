// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  obelisk_sim.design @wait_order_cancel_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 9940000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9940001 in 0 initial hierarchy "top.parent"
    obelisk_sim.code_unit.decl 9940002 in 0 fork hierarchy "top.child"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9940000 : i64} {
      %parent = obelisk_sim.spawn @parent(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @parent(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940001 : i64} {
      %a = obelisk_sim.event.create
      %b = obelisk_sim.event.create
      %child = obelisk_sim.spawn @child(%ctx, %a, %b) :
          !obelisk_sim.context, !obelisk_sim.event, !obelisk_sim.event ->
          !obelisk_sim.process
      %yield = obelisk_sim.time.constant 0
      obelisk_sim.suspend.delay %yield to ^disable(
          %a, %b : !obelisk_sim.event, !obelisk_sim.event)
    ^disable(%resumed_a: !obelisk_sim.event,
             %resumed_b: !obelisk_sim.event):
      obelisk_sim.children.disable
      obelisk_sim.event.trigger %resumed_a nonblocking = false
      obelisk_sim.event.trigger %resumed_b nonblocking = false
      %observe = obelisk_sim.time.constant 0
      obelisk_sim.suspend.delay %observe to ^done
    ^done:
      %message = obelisk_sim.bytes.constant "cancelled wait: PASS"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }

    obelisk_sim.func private @child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9940002 : i64,
                    internal} {
      obelisk_sim.suspend.event_order %a, %b events 2 to ^resumed :
          !obelisk_sim.event, !obelisk_sim.event
    ^resumed:
      %message = obelisk_sim.bytes.constant "cancelled wait: FAIL"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
