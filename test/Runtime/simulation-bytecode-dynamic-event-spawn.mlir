// RUN: talos-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph),encode-talos-sim-to-bytecode{vpi=off require-bytecode=true},convert-talos-sim-processes-to-llvm-coroutines)' \
// RUN:   | talos-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
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
  simulation.design @dynamic_event_spawn {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9910000 in 0 root_initializer
        hierarchy "top.root"
    simulation.code_unit.decl 9910001 in 0 initial hierarchy "top.parent"
    simulation.code_unit.decl 9910002 in 0 fork hierarchy "top.child"

    simulation.func @__talos_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9910000 : i64} {
      %parent = simulation.spawn @parent(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9910001 : i64} {
      %event = simulation.event.create
      %child = simulation.spawn @child(%ctx, %event) :
          !simulation.context, !simulation.event -> !simulation.process
      simulation.suspend.event %event to ^resumed

    ^resumed:
      %message = simulation.bytes.constant "dynamic event resumed"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.finish %ctx, %stdout
      simulation.return
    }

    simulation.func private @child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %event: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9910002 : i64} {
      simulation.event.trigger %event nonblocking = false
      simulation.return
    }
  }
}
