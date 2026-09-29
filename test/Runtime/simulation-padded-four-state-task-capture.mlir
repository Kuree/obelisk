// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// A 40-bit four-state value has five-byte value and unknown planes, but each
// plane has eight-byte ABI alignment in a task frame. The padding between the
// planes is valid and must be accepted by native and bytecode frame validators.
// Keep unknown bits in the value so this also checks that both planes survive
// the task boundary.
// CHECK: 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @padded_four_state_task_capture {
    simulation.scope.decl 0 hierarchy "padded_four_state_task_capture"
    simulation.code_unit.decl 9940000 in 0 root_initializer
        hierarchy "padded_four_state_task_capture.root"
    simulation.code_unit.decl 9940001 in 0 initial
        hierarchy "padded_four_state_task_capture.initial"
    simulation.code_unit.decl 9940002 in 0 task
        hierarchy "padded_four_state_task_capture.callee"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9940000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940001 : i64} {
      %value = simulation.logic.constant 78187493530 : i40, 4294967297 : i40 :
          !simulation.logic<40>
      simulation.task.call @callee(%ctx, %value) arguments 2 to ^done :
          !simulation.context, !simulation.logic<40>
    ^done:
      simulation.return
    }

    simulation.func private @callee(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<40>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 9940002 : i64} {
      %expected = simulation.logic.constant 78187493530 : i40,
          4294967297 : i40 : !simulation.logic<40>
      %ok = simulation.logic.compare case_eq %value, %expected :
          (!simulation.logic<40>, !simulation.logic<40>) -> i1
      %format = simulation.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %ok)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, i1
      simulation.return
    }
  }
}
