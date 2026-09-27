// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=0}))' | FileCheck %s --check-prefix=INLINE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=0}),encode-obelisk-sim-to-bytecode{vpi=off})' | %python %S/../../../Conversion/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @callable_process_kill {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.recursive"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.actor"

    simulation.func private @recursive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32},
        %again: i1 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      cf.cond_br %again, ^recurse, ^kill
    ^recurse:
      %false = arith.constant false
      simulation.call @recursive(%ctx, %process, %false) :
          (!simulation.context, !simulation.process, i1) -> ()
      simulation.return
    ^kill:
      simulation.process.control kill %process to ^continued
    ^continued:
      simulation.return
    }

    simulation.func @actor(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %current = simulation.process.current
      %false = arith.constant false
      simulation.call @recursive(%ctx, %current, %false) :
          (!simulation.context, !simulation.process, i1) -> ()
      simulation.return
    }
  }
}

// A callable kill can either continue synchronously after killing another
// process or unwind the bytecode call stack after killing the current process.
// It therefore remains outlined even when recursion prevents inlining.
// INLINE-LABEL: simulation.func private @recursive(
// INLINE: simulation.call @recursive
// INLINE: simulation.process.control kill %{{.*}} to ^[[CONT:.*]]
// INLINE: ^[[CONT]]:
// INLINE-NEXT: simulation.return

// Opcode 58 is ProcessControl and flag zero is kill. Callable control uses a
// function-local continuation record rather than a process frame.
// BYTECODE: opcode=58 flags=0
// BYTECODE: continuation {{.*}} id=1

// Process control in a zero-time function is status-threaded through the
// ordinary native ABI. It must not make the function a coroutine actor or
// retain a stale frame analysis after ordinary lowering.
// NATIVE-LABEL: llvm.func @recursive(
// NATIVE: %[[CURRENT:.*]] = llvm.call @obelisk_rt_v1_process_current
// NATIVE-NEXT: %[[IS_CURRENT:.*]] = llvm.icmp "eq" %[[CURRENT]], %{{.*}} : i64
// NATIVE-NEXT: llvm.cond_br %[[IS_CURRENT]], ^[[CURRENT_REJECT:[a-zA-Z0-9]+]], ^[[INVOKE:[a-zA-Z0-9]+]]
// NATIVE: ^[[INVOKE]]:
// NATIVE: llvm.call @obelisk_rt_v1_process_control
// NATIVE: ^[[CURRENT_REJECT]]:
// NATIVE-NOT: llvm.call @obelisk_rt_v1_process_control
// NATIVE: llvm.return
// NATIVE-NOT: llvm.intr.coro.
// NATIVE-LABEL: llvm.func @actor
