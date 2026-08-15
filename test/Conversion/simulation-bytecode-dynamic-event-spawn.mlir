// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// A dynamically-created event crosses the spawn ABI, is reconstructed in the
// child frame, and wakes its parent after the child terminates and releases its
// captures. This exercises the dynamic-event handle namespace rather than a
// statically declared event descriptor.
// CHECK: dynamic event resumed

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @dynamic_event_spawn {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 9910000 in 0 root_initializer
        hierarchy "top.root"
    obelisk_sim.code_unit.decl 9910001 in 0 initial hierarchy "top.parent"
    obelisk_sim.code_unit.decl 9910002 in 0 fork hierarchy "top.child"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9910000 : i64} {
      %parent = obelisk_sim.spawn @parent(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @parent(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9910001 : i64} {
      %event = obelisk_sim.event.create
      %child = obelisk_sim.spawn @child(%ctx, %event) :
          !obelisk_sim.context, !obelisk_sim.event -> !obelisk_sim.process
      obelisk_sim.suspend.event %event to ^resumed

    ^resumed:
      %message = obelisk_sim.bytes.constant "dynamic event resumed"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.finish %ctx, %stdout
      obelisk_sim.return
    }

    obelisk_sim.func private @child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %event: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9910002 : i64} {
      obelisk_sim.event.trigger %event nonblocking = false
      obelisk_sim.return
    }
  }
}
