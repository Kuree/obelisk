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

// A finite design may legitimately require more than 2^20 resumptions in one
// time slot. A fixed delta-cycle cap must not turn that execution into an
// out-of-resources failure in either tier.
// CHECK: PASSED

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @long_delta_cycle {
    simulation.scope.decl 0 hierarchy "long_delta_cycle"
    simulation.storage.decl 0 in 0 : !simulation.logic<64> design
        hierarchy "long_delta_cycle.count"
    simulation.code_unit.decl 9972000 in 0 root_initializer
        hierarchy "long_delta_cycle.root"
    simulation.code_unit.decl 9972001 in 0 initial
        hierarchy "long_delta_cycle.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9972000 : i64} {
      %count = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<64>>
      %process = simulation.spawn @initial(%ctx, %count) :
          !simulation.context, !simulation.ref<!simulation.logic<64>> ->
          !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %counter: !simulation.ref<!simulation.logic<64>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9972001 : i64} {
      %zero = simulation.logic.constant 0 : i64, 0 : i64 :
          !simulation.logic<64>
      simulation.ref.store %zero to %counter :
          !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      cf.br ^loop
    ^loop:
      %limit = simulation.logic.constant 1048577 : i64, 0 : i64 :
          !simulation.logic<64>
      %count = simulation.ref.load %counter :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %compared = simulation.logic.compare uge %count, %limit :
          (!simulation.logic<64>, !simulation.logic<64>) ->
          !simulation.logic<1>
      %complete = simulation.logic.is_true %compared :
          !simulation.logic<1>
      cf.cond_br %complete, ^done, ^wait
    ^wait:
      %delay = simulation.time.constant 0
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %current = simulation.ref.load %counter :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %one = simulation.logic.constant 1 : i64, 0 : i64 :
          !simulation.logic<64>
      %next = simulation.logic.binary add %current, %one :
          !simulation.logic<64>
      simulation.ref.store %next to %counter :
          !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      cf.br ^loop
    ^done:
      %passed = simulation.bytes.constant "PASSED"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%passed)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
