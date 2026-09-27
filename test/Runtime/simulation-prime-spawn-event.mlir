// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: %t.exe --execution-tier=native | FileCheck %s

// A prime-on-spawn child must establish its event wait before the spawning
// parent continues. The parent triggers the event in the same activation, so
// an ordinarily queued child would miss it and never print this message.
// CHECK: primed waiter resumed

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @prime_spawn_event {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 9920000 in 0 root_initializer
        hierarchy "top.root"
    obelisk_sim.code_unit.decl 9920001 in 0 initial hierarchy "top.parent"
    obelisk_sim.code_unit.decl 9920002 in 0 fork hierarchy "top.waiter"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9920000 : i64} {
      %parent = obelisk_sim.spawn @parent(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @parent(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9920001 : i64} {
      %event = obelisk_sim.event.create
      %waiter = obelisk_sim.spawn @waiter(%ctx, %event) :
          !obelisk_sim.context, !obelisk_sim.event -> !obelisk_sim.process
      obelisk_sim.event.trigger %event nonblocking = false
      obelisk_sim.return
    }

    obelisk_sim.func private @waiter(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %event: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9920002 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      obelisk_sim.suspend.event %event to ^resumed

    ^resumed:
      %message = obelisk_sim.bytes.constant "primed waiter resumed"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.finish %ctx, %stdout
      obelisk_sim.return
    }
  }
}
