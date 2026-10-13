// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// IEEE 1800-2023 4.6(b), 10.4.2 Example 6: an NBA enqueued at t=0 for t=15
// precedes the assignment executed at t=15. The delayed NBA enters the runtime
// queue, and writer0, which targets the same root, enqueues through that queue
// behind it. The independent writers keep generated scalar staging.
// CHECK-LABEL: llvm.func @observe.__obelisk_coro_ramp(
// CHECK: llvm.call @obelisk_rt_v1_scheduler_nba(
// CHECK-LABEL: llvm.func @writer0.__obelisk_eval_body_0(
// CHECK: llvm.call @obelisk_rt_v1_scheduler_nba(
// CHECK-LABEL: llvm.func @writer1.__obelisk_eval_body_0(
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @writer2.__obelisk_eval_body_0(
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @writer3.__obelisk_eval_body_0(
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.return

!word = !simulation.logic<32>
!ref = !simulation.ref<!word>
!clock = !simulation.ref<i1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 0 : i32} {
  simulation.design @shared_nba {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 initial hierarchy "observe"
    simulation.storage.decl 1 in 0 : !word design
    simulation.code_unit.decl 4 in 0 always hierarchy "writer0"
    simulation.storage.decl 2 in 0 : !word design
    simulation.code_unit.decl 5 in 0 always hierarchy "writer1"
    simulation.storage.decl 3 in 0 : !word design
    simulation.code_unit.decl 6 in 0 always hierarchy "writer2"
    simulation.storage.decl 4 in 0 : !word design
    simulation.code_unit.decl 7 in 0 always hierarchy "writer3"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clock
      %zero = arith.constant false
      simulation.ref.store %zero to %clk : i1, !clock
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clock -> !simulation.process
      %q0 = simulation.context.storage %ctx[1] : !ref
      %v0 = simulation.logic.constant 0 : i32, 0 : i32 : !word
      simulation.ref.store %v0 to %q0 : !word, !ref
      %p0 = simulation.spawn @writer0(%ctx, %clk, %q0) : !simulation.context, !clock, !ref -> !simulation.process
      %q1 = simulation.context.storage %ctx[2] : !ref
      %v1 = simulation.logic.constant 1 : i32, 0 : i32 : !word
      simulation.ref.store %v1 to %q1 : !word, !ref
      %p1 = simulation.spawn @writer1(%ctx, %clk, %q1) : !simulation.context, !clock, !ref -> !simulation.process
      %q2 = simulation.context.storage %ctx[3] : !ref
      %v2 = simulation.logic.constant 2 : i32, 0 : i32 : !word
      simulation.ref.store %v2 to %q2 : !word, !ref
      %p2 = simulation.spawn @writer2(%ctx, %clk, %q2) : !simulation.context, !clock, !ref -> !simulation.process
      %q3 = simulation.context.storage %ctx[4] : !ref
      %v3 = simulation.logic.constant 3 : i32, 0 : i32 : !word
      simulation.ref.store %v3 to %q3 : !word, !ref
      %p3 = simulation.spawn @writer3(%ctx, %clk, %q3) : !simulation.context, !clock, !ref -> !simulation.process
      %observe = simulation.spawn @observe(%ctx, %clk, %q0, %q3) : !simulation.context, !clock, !ref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !clock -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clk : i1, !clock
      cf.br ^wait
    }
    simulation.func @writer0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^update {site = #schedule.continuation<id = 2>} : !clock
    ^update:
      %old = simulation.ref.load %q : !ref -> !word
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !word
      %next = simulation.logic.binary add %old, %one : !word
      simulation.nba.enqueue %next to %q : (!word, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @writer1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^update {site = #schedule.continuation<id = 3>} : !clock
    ^update:
      %old = simulation.ref.load %q : !ref -> !word
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !word
      %next = simulation.logic.binary add %old, %one : !word
      simulation.nba.enqueue %next to %q : (!word, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @writer2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^update {site = #schedule.continuation<id = 4>} : !clock
    ^update:
      %old = simulation.ref.load %q : !ref -> !word
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !word
      %next = simulation.logic.binary add %old, %one : !word
      simulation.nba.enqueue %next to %q : (!word, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @writer3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^update {site = #schedule.continuation<id = 5>} : !clock
    ^update:
      %old = simulation.ref.load %q : !ref -> !word
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !word
      %next = simulation.logic.binary add %old, %one : !word
      simulation.nba.enqueue %next to %q : (!word, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @observe(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %first: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %last: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %hundred = simulation.logic.constant 100 : i32, 0 : i32 : !word
      %due = simulation.time.constant 15
      simulation.nba.enqueue %hundred to %first after %due : (!word, !ref, !simulation.time) -> ()
      %zero = arith.constant 0 : i32
      %firstDelay = simulation.time.constant 5
      simulation.suspend.delay %firstDelay to ^before(%zero : i32) {site = #schedule.continuation<id = 132>, timing = #schedule.timing_site<id = 2, kind = calendar>}
    ^wait(%n: i32):
      simulation.suspend.edge posedge %clk to ^before(%n : i32) {schedule.procedural_event_wait, site = #schedule.continuation<id = 6>} : !clock
    ^before(%iteration: i32):
      %a = simulation.ref.load %first : !ref -> !word
      %b = simulation.ref.load %last : !ref -> !word
      %real = arith.constant 1.0 : f64
      %fmt = simulation.bytes.constant "active %0d %0d %f"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %a, %b, %real) newline = true radix = <decimal> flags = [0, 0, 0, 4] : !simulation.bytes, !word, !word, f64
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^after(%iteration : i32) {site = #schedule.continuation<id = 7>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^after(%previous: i32):
      %c = simulation.ref.load %first : !ref -> !word
      %d = simulation.ref.load %last : !ref -> !word
      %fmt2 = simulation.bytes.constant "settled %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%fmt2, %c, %d) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !word, !word
      %one = arith.constant 1 : i32
      %four = arith.constant 4 : i32
      %next = arith.addi %previous, %one : i32
      %more = arith.cmpi ult, %next, %four : i32
      cf.cond_br %more, ^wait(%next : i32), ^done
    ^done:
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
