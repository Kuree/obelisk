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

// A finite design may legitimately require more than 2^20 resumptions in one
// time slot. A fixed delta-cycle cap must not turn that execution into an
// out-of-resources failure in either tier.
// CHECK: PASSED

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @long_delta_cycle {
    obelisk_sim.scope.decl 0 hierarchy "long_delta_cycle"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<64> design
        hierarchy "long_delta_cycle.count"
    obelisk_sim.code_unit.decl 9972000 in 0 root_initializer
        hierarchy "long_delta_cycle.root"
    obelisk_sim.code_unit.decl 9972001 in 0 initial
        hierarchy "long_delta_cycle.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9972000 : i64} {
      %count = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %process = obelisk_sim.spawn @initial(%ctx, %count) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<64>> ->
          !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %counter: !obelisk_sim.ref<!obelisk_sim.logic<64>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9972001 : i64} {
      %zero = obelisk_sim.logic.constant 0 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      obelisk_sim.ref.store %zero to %counter :
          !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      cf.br ^loop
    ^loop:
      %limit = obelisk_sim.logic.constant 1048577 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %count = obelisk_sim.ref.load %counter :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %compared = obelisk_sim.logic.compare uge %count, %limit :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) ->
          !obelisk_sim.logic<1>
      %complete = obelisk_sim.logic.is_true %compared :
          !obelisk_sim.logic<1>
      cf.cond_br %complete, ^done, ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 0
      obelisk_sim.suspend.delay %delay to ^resume
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
