// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// Pulse rejection keeps at most one calendar entry per destination bit. This
// deliberately schedules 2,001 alternating 512-bit values with a delay longer
// than the whole stimulus. A tombstone-based implementation would retain and
// repeatedly scan about one million stale events before it could terminate.
// CHECK: PASSED

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @partial_specify_path_stress {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : !simulation.logic<64> design
        hierarchy "top.count"
    simulation.net.decl 0 in 0 : !simulation.logic<512> design
        hierarchy "top.output"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<512> design
    simulation.code_unit.decl 9931100 in 0 root_initializer
        hierarchy "top.root"
    simulation.code_unit.decl 9931101 in 0 initial hierarchy "top.test"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9931100 : i64} {
      %count = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<64>>
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<512>>
      %test = simulation.spawn @test(%ctx, %count, %driver) :
          !simulation.context, !simulation.ref<!simulation.logic<64>>,
          !simulation.driver<!simulation.logic<512>> -> !simulation.process
      simulation.return
    }

    simulation.func private @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %counter: !simulation.ref<!simulation.logic<64>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %driver: !simulation.driver<!simulation.logic<512>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9931101 : i64} {
      %zero = simulation.logic.constant 0 : i64, 0 : i64 :
          !simulation.logic<64>
      simulation.ref.store %zero to %counter :
          !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      cf.br ^loop

    ^loop:
      %count = simulation.ref.load %counter :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %limit = simulation.logic.constant 2001 : i64, 0 : i64 :
          !simulation.logic<64>
      %compared = simulation.logic.compare uge %count, %limit :
          (!simulation.logic<64>, !simulation.logic<64>) ->
          !simulation.logic<1>
      %complete = simulation.logic.is_true %compared :
          !simulation.logic<1>
      cf.cond_br %complete, ^done, ^drive

    ^drive:
      %bit = simulation.logic.extract %count from 0 :
          !simulation.logic<64> -> !simulation.logic<1>
      %value = simulation.logic.replicate %bit times 512 :
          !simulation.logic<1> -> !simulation.logic<512>
      %mask = arith.constant -1 : i512
      %delay = simulation.time.constant 10000
      simulation.driver.drive_inertial_path %driver = %value active %mask
          masks[%mask, %mask, %mask] after[%delay, %delay, %delay]
          site 9931101 : 0 group 0 of 1 :
          !simulation.driver<!simulation.logic<512>>,
          !simulation.logic<512>, i512
      %one_tick = simulation.time.constant 1
      simulation.suspend.delay %one_tick to ^resume

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
