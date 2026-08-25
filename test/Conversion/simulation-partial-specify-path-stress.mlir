// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  obelisk_sim.design @partial_specify_path_stress {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<64> design
        hierarchy "top.count"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<512> design
        hierarchy "top.output"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<512> design
    obelisk_sim.code_unit.decl 9931100 in 0 root_initializer
        hierarchy "top.root"
    obelisk_sim.code_unit.decl 9931101 in 0 initial hierarchy "top.test"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9931100 : i64} {
      %count = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<512>>
      %test = obelisk_sim.spawn @test(%ctx, %count, %driver) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<64>>,
          !obelisk_sim.driver<!obelisk_sim.logic<512>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %counter: !obelisk_sim.ref<!obelisk_sim.logic<64>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<512>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9931101 : i64} {
      %zero = obelisk_sim.logic.constant 0 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      obelisk_sim.ref.store %zero to %counter :
          !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      cf.br ^loop

    ^loop:
      %count = obelisk_sim.ref.load %counter :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %limit = obelisk_sim.logic.constant 2001 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %compared = obelisk_sim.logic.compare uge %count, %limit :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) ->
          !obelisk_sim.logic<1>
      %complete = obelisk_sim.logic.is_true %compared :
          !obelisk_sim.logic<1>
      cf.cond_br %complete, ^done, ^drive

    ^drive:
      %bit = obelisk_sim.logic.extract %count from 0 :
          !obelisk_sim.logic<64> -> !obelisk_sim.logic<1>
      %value = obelisk_sim.logic.replicate %bit times 512 :
          !obelisk_sim.logic<1> -> !obelisk_sim.logic<512>
      %mask = arith.constant -1 : i512
      %delay = obelisk_sim.time.constant 10000
      obelisk_sim.driver.drive_inertial_path %driver = %value active %mask
          masks[%mask, %mask, %mask] after[%delay, %delay, %delay]
          site 9931101 : 0 group 0 of 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<512>>,
          !obelisk_sim.logic<512>, i512
      %one_tick = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_tick to ^resume

    ^resume:
      %current = obelisk_sim.ref.load %counter :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %one = obelisk_sim.logic.constant 1 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %next = obelisk_sim.logic.binary add %current, %one :
          !obelisk_sim.logic<64>
      obelisk_sim.ref.store %next to %counter :
          !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      cf.br ^loop

    ^done:
      %passed = obelisk_sim.bytes.constant "PASSED"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%passed)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
