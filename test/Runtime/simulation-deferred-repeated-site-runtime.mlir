// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=auto | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2017 16.4.1 queues one report for every deferred-immediate
// assertion evaluation. Repeated encounters of one site remain independent
// until an explicit process flush point described by 16.4.2.
// CHECK: PASSED
// CHECK-NOT: FAILED

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @deferred_repeated_site {
    obelisk_sim.scope.decl 0 hierarchy "deferred_repeated_site"
    obelisk_sim.code_unit.decl 9981000 in 0 root_initializer
        hierarchy "deferred_repeated_site.root"
    obelisk_sim.code_unit.decl 9981001 in 0 initial
        hierarchy "deferred_repeated_site.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9981000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9981001 : i64} {
      %first_ticket = obelisk_sim.assert.deferred_enqueue 41
      %second_ticket = obelisk_sim.assert.deferred_enqueue 41
      %first = obelisk_sim.assert.deferred_mature %first_ticket : i64
      %second = obelisk_sim.assert.deferred_mature %second_ticket : i64
      %both = arith.andi %first, %second : i1
      cf.cond_br %both, ^passed, ^failed
    ^passed:
      %passed_message = obelisk_sim.bytes.constant "PASSED"
      %passed_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %passed_stdout(%passed_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^failed:
      %failed_message = obelisk_sim.bytes.constant "FAILED"
      %failed_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %failed_stdout(%failed_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
