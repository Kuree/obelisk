// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe > %t.out 2> %t.diagnostics
// RUN: FileCheck %s < %t.out
// RUN: FileCheck %s --check-prefix=TIER < %t.diagnostics
// A persistent procedural clock consumer outlives the finite periodic startup
// budget. Keep Tier-2 fragments native while rearming its runtime subscription;
// reject neither the lifecycle nor NBA writes, and do not exit early.
// PLAN-COUNT-2: llvm.mlir.global internal @__obelisk_eval_nba_valid_
// PLAN-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
// PLAN-LABEL: llvm.func @__obelisk_aot_runtime_nba_commit_v1(
// PLAN: llvm.call @obelisk_rt_v1_static_nba_commit_roots
// TIER: obelisk-periodic-reject={{(fanout|tier3)-bootstrap}}
// TIER: aot_node_executions={{[1-9][0-9]*}}
// TIER-SAME: aot_fallbacks=0
// CHECK: 00000055 00550000
// CHECK-NEXT: 00000055 00550000
// CHECK-NEXT: 00000055 00550000
// CHECK-NEXT: 00550055 00550000

!words = !obelisk_sim.unpacked_array<0 : 31 x !obelisk_sim.logic<32>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @persistent_clock_consumer {
    obelisk_sim.scope.decl 0 hierarchy "persistent_clock_consumer"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "persistent_clock_consumer.root"
    obelisk_sim.code_unit.decl 2 in 0 always
        hierarchy "persistent_clock_consumer.clock"
    obelisk_sim.code_unit.decl 3 in 0 always
        hierarchy "persistent_clock_consumer.update"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "persistent_clock_consumer.check"
    obelisk_sim.code_unit.decl 5 in 0 initial hierarchy "persistent_clock_consumer.consumer"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "persistent_clock_consumer.clock"
    obelisk_sim.storage.decl 1 in 0 : !words design hierarchy "persistent_clock_consumer.data"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<64> design hierarchy "persistent_clock_consumer.index"
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<64> design hierarchy "persistent_clock_consumer.other_index"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %data = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!words>
      %index = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %otherIndex = obelisk_sim.context.storage %ctx[3] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %zeroClock = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zeroClock to %clock : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %zeroIndex = obelisk_sim.logic.constant 0 : i64, 0 : i64 : !obelisk_sim.logic<64>
      %oneIndex = obelisk_sim.logic.constant 1 : i64, 0 : i64 : !obelisk_sim.logic<64>
      obelisk_sim.ref.store %zeroIndex to %index : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %oneIndex to %otherIndex : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %zeroWord = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %word0 = obelisk_sim.ref.subelement %data[[0]] : !obelisk_sim.ref<!words> -> !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %word1 = obelisk_sim.ref.subelement %data[[1]] : !obelisk_sim.ref<!words> -> !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.ref.store %zeroWord to %word0 : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.ref.store %zeroWord to %word1 : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %consumer = obelisk_sim.spawn @consumer(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %check = obelisk_sim.spawn @check(%ctx, %data, %index, %otherIndex) :
          !obelisk_sim.context, !obelisk_sim.ref<!words>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>>, !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.process
      %c = obelisk_sim.spawn @clock(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %p = obelisk_sim.spawn @update(%ctx, %clock, %data, %index, %otherIndex) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!words>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #obelisk_sim.continuation<id = 1>,
           timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clock :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clock : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }

    obelisk_sim.func @update(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %data: !obelisk_sim.ref<!words>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %index: !obelisk_sim.ref<!obelisk_sim.logic<64>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64},
        %otherIndex: !obelisk_sim.ref<!obelisk_sim.logic<64>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume
          {site = #obelisk_sim.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %low = obelisk_sim.ref.load %index :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %element = obelisk_sim.ref.array_element %data[%low] :
          (!obelisk_sim.ref<!words>, !obelisk_sim.logic<64>) ->
          !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %lo = obelisk_sim.ref.extract %element from 0 :
          !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %otherLow = obelisk_sim.ref.load %otherIndex :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %otherElement = obelisk_sim.ref.array_element %data[%otherLow] :
          (!obelisk_sim.ref<!words>, !obelisk_sim.logic<64>) ->
          !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %hi = obelisk_sim.ref.extract %otherElement from 16 :
          !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %value = obelisk_sim.logic.constant 85 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %value to %lo :
          (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      obelisk_sim.nba.enqueue %value to %hi :
          (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      cf.br ^wait
    }
    obelisk_sim.func @consumer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %count = arith.constant 5000 : i64
      cf.br ^repeat(%count : i64)
    ^repeat(%remaining: i64):
      %zeroCount = arith.constant 0 : i64
      %pending = arith.cmpi sgt, %remaining, %zeroCount : i64
      cf.cond_br %pending, ^waitClock, ^done
    ^waitClock:
      obelisk_sim.suspend.edge posedge %clock to ^next(%remaining : i64)
          {obelisk_sim.procedural_event_wait} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^next(%previous: i64):
      %oneCount = arith.constant 1 : i64
      %nextCount = arith.subi %previous, %oneCount : i64
      cf.br ^repeat(%nextCount : i64)
    ^done:
      obelisk_sim.return
    }
    obelisk_sim.func @check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %data: !obelisk_sim.ref<!words> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %index: !obelisk_sim.ref<!obelisk_sim.logic<64>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %otherIndex: !obelisk_sim.ref<!obelisk_sim.logic<64>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = obelisk_sim.time.constant 10003
      obelisk_sim.suspend.delay %delay to ^separate
    ^separate:
      %word0 = obelisk_sim.ref.subelement %data[[0]] : !obelisk_sim.ref<!words> -> !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %word1 = obelisk_sim.ref.subelement %data[[1]] : !obelisk_sim.ref<!words> -> !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %first0 = obelisk_sim.ref.load %word0 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %first1 = obelisk_sim.ref.load %word1 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %format = obelisk_sim.bytes.constant "%08h %08h"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %first0, %first1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %invalid = obelisk_sim.logic.constant 32 : i64, 0 : i64 : !obelisk_sim.logic<64>
      %negative = obelisk_sim.logic.constant -1 : i64, 0 : i64 : !obelisk_sim.logic<64>
      obelisk_sim.ref.store %invalid to %index : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %negative to %otherIndex : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two to ^outside
    ^outside:
      %outside0 = obelisk_sim.ref.load %word0 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %outside1 = obelisk_sim.ref.load %word1 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %stdout(%format, %outside0, %outside1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %unknown = obelisk_sim.logic.constant 0 : i64, -1 : i64 : !obelisk_sim.logic<64>
      obelisk_sim.ref.store %unknown to %index : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %unknown to %otherIndex : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.suspend.delay %two to ^unknown
    ^unknown:
      %unknown0 = obelisk_sim.ref.load %word0 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %unknown1 = obelisk_sim.ref.load %word1 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %stdout(%format, %unknown0, %unknown1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %zero = obelisk_sim.logic.constant 0 : i64, 0 : i64 : !obelisk_sim.logic<64>
      obelisk_sim.ref.store %zero to %index : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %zero to %otherIndex : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.suspend.delay %two to ^same
    ^same:
      %same0 = obelisk_sim.ref.load %word0 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %same1 = obelisk_sim.ref.load %word1 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %stdout(%format, %same0, %same1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
