// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o0.o
// RUN: %llvm_dist/bin/clang++ %t.o0.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: %t.o0.exe --execution-tier=native | FileCheck %s --implicit-check-not=FAIL
// RUN: %t.o0.exe --execution-tier=bytecode | FileCheck %s --implicit-check-not=FAIL
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3},obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o3.o
// RUN: %llvm_dist/bin/clang++ %t.o3.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o3.exe
// RUN: %t.o3.exe --execution-tier=native | FileCheck %s --implicit-check-not=FAIL
// RUN: %t.o3.exe --execution-tier=bytecode | FileCheck %s --implicit-check-not=FAIL

// Hand-authored simulation IR models both control-flow outcomes of the static
// detached actor emitted for `@(source) first ##1 @(destination) second`. The
// first predicate is tested before any suspension. The false age-zero path
// branches away from the destination wait and cannot be resurrected; the true
// path waits for the destination event before reporting success.
// CHECK-COUNT-1: multiclock immediate runtime: PASS

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @multiclock_immediate_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9970000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9970001 in 0 initial hierarchy "top.test"
    simulation.code_unit.decl 9970002 in 0 fork hierarchy "top.destination_tick"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64} {
      %test = simulation.spawn @test(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970001 : i64} {
      %destination = simulation.event.create
      %tick = simulation.spawn @destination_tick(%ctx, %destination) :
          !simulation.context, !simulation.event -> !simulation.process
      %false_first = simulation.logic.constant false, false :
          !simulation.logic<1>
      %false_matches = simulation.logic.is_true %false_first :
          !simulation.logic<1>
      cf.cond_br %false_matches, ^resurrected, ^test_true
    ^test_true:
      %true_first = simulation.logic.constant true, false :
          !simulation.logic<1>
      %true_matches = simulation.logic.is_true %true_first :
          !simulation.logic<1>
      cf.cond_br %true_matches, ^wait_destination, ^resurrected
    ^wait_destination:
      simulation.suspend.event %destination to ^matched
    ^matched:
      %pass_message = simulation.bytes.constant "multiclock immediate runtime: PASS"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%pass_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    ^resurrected:
      %fail_message = simulation.bytes.constant "multiclock immediate runtime: FAIL"
      %fail_stdout = arith.constant 1 : i32
      simulation.display %ctx to %fail_stdout(%fail_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      %fail_zero = arith.constant 0 : i32
      simulation.finish %ctx, %fail_zero
      simulation.return
    }

    simulation.func private @destination_tick(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9970002 : i64} {
      simulation.event.trigger %destination nonblocking = false
      simulation.return
    }
  }
}
