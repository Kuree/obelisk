// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  obelisk_sim.design @external_task_disable {
    obelisk_sim.scope.decl 0 hierarchy "external_task_disable"
    obelisk_sim.code_unit.decl 9970000 in 0 root_initializer
        hierarchy "external_task_disable.root"
    obelisk_sim.code_unit.decl 9970001 in 0 task
        hierarchy "external_task_disable.worker"
    obelisk_sim.code_unit.decl 9970002 in 0 initial
        hierarchy "external_task_disable.caller"
    obelisk_sim.code_unit.decl 9970003 in 0 initial
        hierarchy "external_task_disable.disabler"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64} {
      %caller = obelisk_sim.spawn @caller(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %disabler = obelisk_sim.spawn @disabler(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @worker(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 9970001 : i64,
                    obelisk_sim.control_target_id = 1 : i64} {
      %activation = obelisk_sim.control.enter 1
      %delay = obelisk_sim.time.constant 10
      obelisk_sim.suspend.delay %delay to ^resume(
          %activation : !obelisk_sim.control)
    ^resume(%resumed: !obelisk_sim.control):
      obelisk_sim.control.leave %resumed
      obelisk_sim.return
    }

    obelisk_sim.func private @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970002 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^call
    ^call:
      obelisk_sim.task.call @worker(%ctx) arguments 1 to ^done :
          !obelisk_sim.context
    ^done:
      %format = obelisk_sim.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      %one = arith.constant true
      obelisk_sim.display %ctx to %stdout(%format, %one)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, i1
      obelisk_sim.return
    }

    obelisk_sim.func private @disabler(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970003 : i64} {
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^disable
    ^disable:
      obelisk_sim.control.disable 1 {hierarchical = true}
      obelisk_sim.return
    }
  }
}
