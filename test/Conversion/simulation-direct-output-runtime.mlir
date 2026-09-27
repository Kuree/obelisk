// RUN: obelisk-opt %s --obelisk-sim-materialize-clocked-control | FileCheck %s --check-prefix=STATE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-materialize-clocked-control,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=NBAKNOWN < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=BARRIER < %t.llvm.mlir
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-materialize-clocked-control,obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=read},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.read.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.read.llvm.mlir
// RUN: FileCheck %s --check-prefix=NBAKNOWN < %t.read.llvm.mlir
// RUN: FileCheck %s --check-prefix=VPIBARRIER < %t.read.llvm.mlir
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-materialize-clocked-control,obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.full.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.full.llvm.mlir
// RUN: FileCheck %s --check-prefix=NBAKNOWN < %t.full.llvm.mlir
// RUN: FileCheck %s --check-prefix=VPIBARRIER < %t.full.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-direct-output-runtime.test.

// Writable capability alone must likewise preserve Tier 1: no writer or
// callback is attached in this fixture. Actual mutation is a cold handoff.
// Read-only VPI capability without live readers must retain the same Tier-1
// loop, direct display, and NBA fast paths. The node/checkpoint counts must
// stay bounded by startup/reset/finish, not grow with the 2501 displays.
// Ordinary stdout snapshots remain inside the clock-group evaluator. Prints
// must not replay NBA effects or execute in dry-run promotion probes. Only
// the deliberate reset/finish runtime work leaves the generated evaluator.
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
// PLAN: llvm.call @obelisk_rt_v1_eval_display
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// A terminating checkpoint returns to finals without re-entering the model.

// The known-state predicate must bypass every accumulator in an empty bitmap
// word, as occurs in the post-NBA combinational fixpoint. The dirty-root path
// remains separate and retains its canonical/staged unknown checks.
// NBAKNOWN-LABEL: llvm.func internal @__obelisk_eval_nba_known_v1
// NBAKNOWN: %[[DIRTY:.*]] = llvm.and {{.*}} : i64
// NBAKNOWN-NEXT: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64)
// NBAKNOWN-NEXT: %[[EMPTY:.*]] = llvm.icmp "eq" %[[DIRTY]], %[[ZERO]] : i64
// NBAKNOWN-NEXT: llvm.cond_br %[[EMPTY]], ^[[NEXT:bb[0-9]+]], ^[[INSPECT:bb[0-9]+]]
// NBAKNOWN: ^[[INSPECT]]:
// NBAKNOWN: llvm.cond_br
// NBAKNOWN: ^[[NEXT]]:
// NBAKNOWN-NEXT: llvm.br
// NBAKNOWN: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_
// NBAKNOWN: llvm.mlir.addressof @__obelisk_state_unknown

// A fixed-only design skips the NBA barrier when its bitmap is empty, then
// checks for dirty local route proofs at the coordinator boundary.
// BARRIER-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// BARRIER: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// BARRIER: %[[DIRTY:.*]] = llvm.load {{.*}} : !llvm.ptr -> i64
// BARRIER-NEXT: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64)
// BARRIER-NEXT: %[[EMPTY:.*]] = llvm.icmp "eq" %[[DIRTY]], %[[ZERO]] : i64
// BARRIER-NEXT: %[[OK:.*]] = llvm.mlir.constant(0 : i32)
// BARRIER-NEXT: llvm.cond_br %[[EMPTY]], ^[[DONE:bb[0-9]+]](%[[OK]] : i32), ^[[COMMIT:bb[0-9]+]]
// BARRIER-NEXT: ^[[COMMIT]]:
// BARRIER: ^[[DONE]](%[[STATUS:.*]]: i32):
// BARRIER-NEXT: %[[BOUNDARY:.*]] = llvm.call @__obelisk_eval_route_promotion_boundary_v1(%[[STATUS]]) : (i32) -> i32
// BARRIER-NEXT: llvm.return %[[BOUNDARY]] : i32

// With VPI a value-change callback can observe every NBA update (IEEE
// 1800-2017 38.36.1), so the updates use the ordered queue and its count
// joins the empty-barrier test.
// VPIBARRIER-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// VPIBARRIER: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// VPIBARRIER: %[[DIRTY:.*]] = llvm.load {{.*}} : !llvm.ptr -> i64
// VPIBARRIER-NEXT: llvm.mlir.addressof @__obelisk_eval_ordered_nba_queue_v1
// VPIBARRIER: %[[COUNT:.*]] = llvm.load {{.*}} : !llvm.ptr -> i32
// VPIBARRIER-NEXT: %[[WIDE:.*]] = llvm.zext %[[COUNT]] : i32 to i64
// VPIBARRIER-NEXT: %[[PENDING:.*]] = llvm.or %[[DIRTY]], %[[WIDE]] : i64
// VPIBARRIER-NEXT: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64)
// VPIBARRIER-NEXT: %[[EMPTY:.*]] = llvm.icmp "eq" %[[PENDING]], %[[ZERO]] : i64
// VPIBARRIER: llvm.cond_br %[[EMPTY]]

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
      %message = simulation.bytes.constant "tick %b"
      %unknown = simulation.logic.constant 9 : i4, 3 : i4 : !simulation.logic<4>
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%message, %unknown) newline = true radix = <decimal> flags = [0, 0] {scope = "clocked_control"} : !simulation.bytes, !simulation.logic<4>
      cf.br ^wait
    }
  }
}
