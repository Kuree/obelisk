// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// Disabling a suspended task from another logical process terminates that
// task activation, not the task's caller. The caller resumes at the task-call
// continuation in both execution tiers.
// CHECK: 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @external_task_disable {
    simulation.scope.decl 0 hierarchy "external_task_disable"
    simulation.code_unit.decl 9970000 in 0 root_initializer
        hierarchy "external_task_disable.root"
    simulation.code_unit.decl 9970001 in 0 task
        hierarchy "external_task_disable.worker"
    simulation.code_unit.decl 9970002 in 0 initial
        hierarchy "external_task_disable.caller"
    simulation.code_unit.decl 9970003 in 0 initial
        hierarchy "external_task_disable.disabler"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64} {
      %caller = simulation.spawn @caller(%ctx) :
          !simulation.context -> !simulation.process
      %disabler = simulation.spawn @disabler(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @worker(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 9970001 : i64,
                    simulation.control_target_id = 1 : i64} {
      %activation = simulation.control.enter 1
      %delay = simulation.time.constant 10
      simulation.suspend.delay %delay to ^resume(
          %activation : !simulation.control)
    ^resume(%resumed: !simulation.control):
      simulation.control.leave %resumed
      simulation.return
    }

    simulation.func private @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970002 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^call
    ^call:
      simulation.task.call @worker(%ctx) arguments 1 to ^done :
          !simulation.context
    ^done:
      %format = simulation.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      %one = arith.constant true
      simulation.display %ctx to %stdout(%format, %one)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, i1
      simulation.return
    }

    simulation.func private @disabler(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970003 : i64} {
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^disable
    ^disable:
      simulation.control.disable 1 {hierarchical = true}
      simulation.return
    }
  }
}
