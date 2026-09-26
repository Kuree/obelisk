// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o0.o
// RUN: %llvm_dist/bin/clang++ %t.o0.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: %t.o0.exe --execution-tier=native | FileCheck %s --implicit-check-not=FAIL
// RUN: %t.o0.exe --execution-tier=bytecode | FileCheck %s --implicit-check-not=FAIL
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3},obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o3.o
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
  obelisk_sim.design @multiclock_immediate_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 9970000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9970001 in 0 initial hierarchy "top.test"
    obelisk_sim.code_unit.decl 9970002 in 0 fork hierarchy "top.destination_tick"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64} {
      %test = obelisk_sim.spawn @test(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970001 : i64} {
      %destination = obelisk_sim.event.create
      %tick = obelisk_sim.spawn @destination_tick(%ctx, %destination) :
          !obelisk_sim.context, !obelisk_sim.event -> !obelisk_sim.process
      %false_first = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %false_matches = obelisk_sim.logic.is_true %false_first :
          !obelisk_sim.logic<1>
      cf.cond_br %false_matches, ^resurrected, ^test_true
    ^test_true:
      %true_first = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      %true_matches = obelisk_sim.logic.is_true %true_first :
          !obelisk_sim.logic<1>
      cf.cond_br %true_matches, ^wait_destination, ^resurrected
    ^wait_destination:
      obelisk_sim.suspend.event %destination to ^matched
    ^matched:
      %pass_message = obelisk_sim.bytes.constant "multiclock immediate runtime: PASS"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%pass_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    ^resurrected:
      %fail_message = obelisk_sim.bytes.constant "multiclock immediate runtime: FAIL"
      %fail_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %fail_stdout(%fail_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      %fail_zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %fail_zero
      obelisk_sim.return
    }

    obelisk_sim.func private @destination_tick(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %destination: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9970002 : i64} {
      obelisk_sim.event.trigger %destination nonblocking = false
      obelisk_sim.return
    }
  }
}
