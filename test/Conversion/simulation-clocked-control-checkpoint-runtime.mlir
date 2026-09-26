// RUN: obelisk-opt %s --obelisk-sim-materialize-clocked-control | FileCheck %s --check-prefix=STATE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-materialize-clocked-control,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-clocked-control-checkpoint-runtime.test.

// A checkpointing actor precedes the clock-control owner in dispatch order.
// An early checkpoint must leave that direct owner's pending edge visible to
// the callback's same-slot coordinator, without replaying completed owners.
// STATE: obelisk_sim.storage.decl 2 in 0 : i32 design {observability = 0 : i32}
// STATE: obelisk_sim.storage.decl 3 in 0 : i64 design {observability = 0 : i32}
// STATE: obelisk_sim.storage.decl 4 in 0 : i64 design {observability = 0 : i32}
// STATE-LABEL: obelisk_sim.func @z_consumer
// STATE-SAME: obelisk_sim.clocked_control
// STATE: obelisk_sim.ref.store
// STATE-COUNT-1: obelisk_sim.suspend.edge posedge
// STATE-NOT: obelisk_sim.suspend.edge
// STATE-LABEL: obelisk_sim.func @report
// PLAN: __obelisk_eval_body
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @clocked_control {
    obelisk_sim.scope.decl 0 hierarchy "clocked_control"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "clocked_control.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clocked_control.clock"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "clocked_control.z_consumer"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "clocked_control.report"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zero to %clock : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %bit = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero to %bit : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %p = obelisk_sim.spawn @z_consumer(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %report = obelisk_sim.spawn @report(%ctx, %clock, %bit) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %c = obelisk_sim.spawn @clock(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clock : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clock : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
    obelisk_sim.func @z_consumer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %channel = arith.constant 1 : i32
      %startup = obelisk_sim.bytes.constant "startup"
      obelisk_sim.display %ctx to %channel(%startup) newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      %count = arith.constant 3 : i64
      cf.br ^first(%count : i64)
    ^first(%n: i64):
      obelisk_sim.suspend.edge posedge %clock to ^firstNext(%n : i64)
          {obelisk_sim.procedural_event_wait, site = #obelisk_sim.continuation<id = 2>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^firstNext(%previous: i64):
      %one = arith.constant 1 : i64
      %zeroCount = arith.constant 0 : i64
      %remaining = arith.subi %previous, %one : i64
      %pending = arith.cmpi sgt, %remaining, %zeroCount : i64
      cf.cond_br %pending, ^first(%remaining : i64), ^reset
    ^reset:
      %resetTime = obelisk_sim.time.now %ctx
      %resetFormat = obelisk_sim.bytes.constant "reset %0d"
      obelisk_sim.display %ctx to %channel(%resetFormat, %resetTime) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, i64
      %largeCount = arith.constant 5000 : i64
      cf.br ^second(%largeCount : i64)
    ^second(%m: i64):
      obelisk_sim.suspend.edge posedge %clock to ^secondNext(%m : i64)
          {obelisk_sim.procedural_event_wait, site = #obelisk_sim.continuation<id = 3>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^secondNext(%oldCount: i64):
      %step = arith.constant 1 : i64
      %end = arith.constant 0 : i64
      %left = arith.subi %oldCount, %step : i64
      %more = arith.cmpi sgt, %left, %end : i64
      cf.cond_br %more, ^second(%left : i64), ^done
    ^done:
      %doneTime = obelisk_sim.time.now %ctx
      %format = obelisk_sim.bytes.constant "done %0d"
      obelisk_sim.display %ctx to %channel(%format, %doneTime) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, i64
      %status = arith.constant 1 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
    obelisk_sim.func @report(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %bit: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^tick
          {site = #obelisk_sim.continuation<id = 4>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^tick:
      %old = obelisk_sim.ref.load %bit : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %new to %bit {site = #obelisk_sim.nba_site<id = 0, commit = 0, storage = fixed_slot>} : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %print = obelisk_sim.logic.is_true %old : !obelisk_sim.logic<1>
      cf.cond_br %print, ^report, ^wait
    ^report:
      %message = obelisk_sim.bytes.constant "tick"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%message) newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      cf.br ^wait
    }
  }
}
