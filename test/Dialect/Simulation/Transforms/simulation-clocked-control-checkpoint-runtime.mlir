// RUN: obelisk-opt %s --obelisk-sim-materialize-clocked-control | FileCheck %s --check-prefix=STATE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-materialize-clocked-control,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-clocked-control-checkpoint-runtime.test.

// A checkpointing actor precedes the clock-control owner in dispatch order.
// An early checkpoint must leave that direct owner's pending edge visible to
// the callback's same-slot coordinator, without replaying completed owners.
// STATE: simulation.storage.decl 2 in 0 : i32 design {observability = 0 : i32}
// STATE: simulation.storage.decl 3 in 0 : i64 design {observability = 0 : i32}
// STATE: simulation.storage.decl 4 in 0 : i64 design {observability = 0 : i32}
// STATE-LABEL: simulation.func @z_consumer
// STATE-SAME: schedule.clocked_control
// STATE: simulation.ref.store
// STATE-COUNT-1: simulation.suspend.edge posedge
// STATE-NOT: simulation.suspend.edge
// STATE-LABEL: simulation.func @report
// PLAN: __obelisk_eval_body
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @clocked_control {
    simulation.scope.decl 0 hierarchy "clocked_control"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "clocked_control.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clocked_control.clock"
    simulation.code_unit.decl 3 in 0 initial hierarchy "clocked_control.z_consumer"
    simulation.code_unit.decl 4 in 0 always hierarchy "clocked_control.report"
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.ref.store %zero to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %bit = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %zero to %bit : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %p = simulation.spawn @z_consumer(%ctx, %clock) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %report = simulation.spawn @report(%ctx, %clock, %bit) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %c = simulation.spawn @clock(%ctx, %clock) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @z_consumer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %channel = arith.constant 1 : i32
      %startup = simulation.bytes.constant "startup"
      simulation.display %ctx to %channel(%startup) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      %count = arith.constant 3 : i64
      cf.br ^first(%count : i64)
    ^first(%n: i64):
      simulation.suspend.edge posedge %clock to ^firstNext(%n : i64)
          {schedule.procedural_event_wait, site = #schedule.continuation<id = 2>} : !simulation.ref<!simulation.logic<1>>
    ^firstNext(%previous: i64):
      %one = arith.constant 1 : i64
      %zeroCount = arith.constant 0 : i64
      %remaining = arith.subi %previous, %one : i64
      %pending = arith.cmpi sgt, %remaining, %zeroCount : i64
      cf.cond_br %pending, ^first(%remaining : i64), ^reset
    ^reset:
      %resetTime = simulation.time.now %ctx
      %resetFormat = simulation.bytes.constant "reset %0d"
      simulation.display %ctx to %channel(%resetFormat, %resetTime) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i64
      %largeCount = arith.constant 5000 : i64
      cf.br ^second(%largeCount : i64)
    ^second(%m: i64):
      simulation.suspend.edge posedge %clock to ^secondNext(%m : i64)
          {schedule.procedural_event_wait, site = #schedule.continuation<id = 3>} : !simulation.ref<!simulation.logic<1>>
    ^secondNext(%oldCount: i64):
      %step = arith.constant 1 : i64
      %end = arith.constant 0 : i64
      %left = arith.subi %oldCount, %step : i64
      %more = arith.cmpi sgt, %left, %end : i64
      cf.cond_br %more, ^second(%left : i64), ^done
    ^done:
      %doneTime = simulation.time.now %ctx
      %format = simulation.bytes.constant "done %0d"
      simulation.display %ctx to %channel(%format, %doneTime) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i64
      %status = arith.constant 1 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
    simulation.func @report(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %bit: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^tick
          {site = #schedule.continuation<id = 4>} : !simulation.ref<!simulation.logic<1>>
    ^tick:
      %old = simulation.ref.load %bit : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.nba.enqueue %new to %bit {site = #schedule.nba_site<id = 0, commit = 0, storage = fixed_slot>} : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      %print = simulation.logic.is_true %old : !simulation.logic<1>
      cf.cond_br %print, ^report, ^wait
    ^report:
      %message = simulation.bytes.constant "tick"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%message) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      cf.br ^wait
    }
  }
}
