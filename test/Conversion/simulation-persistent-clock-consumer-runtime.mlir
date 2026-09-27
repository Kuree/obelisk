// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-persistent-clock-consumer-runtime.test.

// A persistent procedural clock consumer outlives the finite periodic startup
// budget. Keep Tier-2 fragments native while rearming its runtime subscription;
// reject neither the lifecycle nor NBA writes, and do not exit early.
// PLAN-COUNT-2: llvm.mlir.global internal @__obelisk_eval_nba_valid_
// PLAN-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
// PLAN-LABEL: llvm.func @__obelisk_aot_runtime_nba_commit_v1(
// PLAN: llvm.call @obelisk_rt_v1_static_nba_commit_roots

!words = !simulation.unpacked_array<0 : 31 x !simulation.logic<32>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @persistent_clock_consumer {
    simulation.scope.decl 0 hierarchy "persistent_clock_consumer"
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "persistent_clock_consumer.root"
    simulation.code_unit.decl 2 in 0 always
        hierarchy "persistent_clock_consumer.clock"
    simulation.code_unit.decl 3 in 0 always
        hierarchy "persistent_clock_consumer.update"
    simulation.code_unit.decl 4 in 0 initial hierarchy "persistent_clock_consumer.check"
    simulation.code_unit.decl 5 in 0 initial hierarchy "persistent_clock_consumer.consumer"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design hierarchy "persistent_clock_consumer.clock"
    simulation.storage.decl 1 in 0 : !words design hierarchy "persistent_clock_consumer.data"
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design hierarchy "persistent_clock_consumer.index"
    simulation.storage.decl 3 in 0 : !simulation.logic<64> design hierarchy "persistent_clock_consumer.other_index"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %data = simulation.context.storage %ctx[1] :
          !simulation.ref<!words>
      %index = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<64>>
      %otherIndex = simulation.context.storage %ctx[3] :
          !simulation.ref<!simulation.logic<64>>
      %zeroClock = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      simulation.ref.store %zeroClock to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %zeroIndex = simulation.logic.constant 0 : i64, 0 : i64 : !simulation.logic<64>
      %oneIndex = simulation.logic.constant 1 : i64, 0 : i64 : !simulation.logic<64>
      simulation.ref.store %zeroIndex to %index : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      simulation.ref.store %oneIndex to %otherIndex : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      %zeroWord = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      %word0 = simulation.ref.subelement %data[[0]] : !simulation.ref<!words> -> !simulation.ref<!simulation.logic<32>>
      %word1 = simulation.ref.subelement %data[[1]] : !simulation.ref<!words> -> !simulation.ref<!simulation.logic<32>>
      simulation.ref.store %zeroWord to %word0 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      simulation.ref.store %zeroWord to %word1 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %consumer = simulation.spawn @consumer(%ctx, %clock) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %check = simulation.spawn @check(%ctx, %data, %index, %otherIndex) :
          !simulation.context, !simulation.ref<!words>,
          !simulation.ref<!simulation.logic<64>>, !simulation.ref<!simulation.logic<64>> -> !simulation.process
      %c = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %p = simulation.spawn @update(%ctx, %clock, %data, %index, %otherIndex) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!words>,
          !simulation.ref<!simulation.logic<64>>,
          !simulation.ref<!simulation.logic<64>> -> !simulation.process
      simulation.return
    }

    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @update(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %data: !simulation.ref<!words>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %index: !simulation.ref<!simulation.logic<64>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64},
        %otherIndex: !simulation.ref<!simulation.logic<64>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume
          {site = #schedule.continuation<id = 2>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %low = simulation.ref.load %index :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %element = simulation.ref.array_element %data[%low] :
          (!simulation.ref<!words>, !simulation.logic<64>) ->
          !simulation.ref<!simulation.logic<32>>
      %lo = simulation.ref.extract %element from 0 :
          !simulation.ref<!simulation.logic<32>> -> !simulation.ref<!simulation.logic<8>>
      %otherLow = simulation.ref.load %otherIndex :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %otherElement = simulation.ref.array_element %data[%otherLow] :
          (!simulation.ref<!words>, !simulation.logic<64>) ->
          !simulation.ref<!simulation.logic<32>>
      %hi = simulation.ref.extract %otherElement from 16 :
          !simulation.ref<!simulation.logic<32>> -> !simulation.ref<!simulation.logic<8>>
      %value = simulation.logic.constant 85 : i8, 0 : i8 : !simulation.logic<8>
      simulation.nba.enqueue %value to %lo :
          (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.nba.enqueue %value to %hi :
          (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }
    simulation.func @consumer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %count = arith.constant 5000 : i64
      cf.br ^repeat(%count : i64)
    ^repeat(%remaining: i64):
      %zeroCount = arith.constant 0 : i64
      %pending = arith.cmpi sgt, %remaining, %zeroCount : i64
      cf.cond_br %pending, ^waitClock, ^done
    ^waitClock:
      simulation.suspend.edge posedge %clock to ^next(%remaining : i64)
          {schedule.procedural_event_wait} : !simulation.ref<!simulation.logic<1>>
    ^next(%previous: i64):
      %oneCount = arith.constant 1 : i64
      %nextCount = arith.subi %previous, %oneCount : i64
      cf.br ^repeat(%nextCount : i64)
    ^done:
      simulation.return
    }
    simulation.func @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %data: !simulation.ref<!words> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %index: !simulation.ref<!simulation.logic<64>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %otherIndex: !simulation.ref<!simulation.logic<64>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 10003
      simulation.suspend.delay %delay to ^separate
    ^separate:
      %word0 = simulation.ref.subelement %data[[0]] : !simulation.ref<!words> -> !simulation.ref<!simulation.logic<32>>
      %word1 = simulation.ref.subelement %data[[1]] : !simulation.ref<!words> -> !simulation.ref<!simulation.logic<32>>
      %first0 = simulation.ref.load %word0 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %first1 = simulation.ref.load %word1 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %format = simulation.bytes.constant "%08h %08h"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %first0, %first1) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<32>, !simulation.logic<32>
      %invalid = simulation.logic.constant 32 : i64, 0 : i64 : !simulation.logic<64>
      %negative = simulation.logic.constant -1 : i64, 0 : i64 : !simulation.logic<64>
      simulation.ref.store %invalid to %index : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      simulation.ref.store %negative to %otherIndex : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      %two = simulation.time.constant 2
      simulation.suspend.delay %two to ^outside
    ^outside:
      %outside0 = simulation.ref.load %word0 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %outside1 = simulation.ref.load %word1 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.display %ctx to %stdout(%format, %outside0, %outside1) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<32>, !simulation.logic<32>
      %unknown = simulation.logic.constant 0 : i64, -1 : i64 : !simulation.logic<64>
      simulation.ref.store %unknown to %index : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      simulation.ref.store %unknown to %otherIndex : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      simulation.suspend.delay %two to ^unknown
    ^unknown:
      %unknown0 = simulation.ref.load %word0 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %unknown1 = simulation.ref.load %word1 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.display %ctx to %stdout(%format, %unknown0, %unknown1) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<32>, !simulation.logic<32>
      %zero = simulation.logic.constant 0 : i64, 0 : i64 : !simulation.logic<64>
      simulation.ref.store %zero to %index : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      simulation.ref.store %zero to %otherIndex : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      simulation.suspend.delay %two to ^same
    ^same:
      %same0 = simulation.ref.load %word0 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %same1 = simulation.ref.load %word1 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.display %ctx to %stdout(%format, %same0, %same1) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<32>, !simulation.logic<32>
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
