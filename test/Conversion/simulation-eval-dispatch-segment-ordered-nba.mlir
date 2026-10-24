// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-replicate,simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// A wide NBA root with an edge observer must retain ordered update events
// (IEEE 1800-2023 4.6(b), 9.4.2 and 10.4.2). An enqueue allocation failure
// must stop the local loop before another owner runs. The outer dispatcher
// propagates that same status instead of entering the NBA commit path.
// CHECK: llvm.mlir.global internal @__obelisk_eval_ordered_nba_queue_v1
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// CHECK: %[[QUEUE:.*]] = llvm.mlir.addressof @__obelisk_eval_ordered_nba_queue_v1
// CHECK: %[[STATUS_PTR:.*]] = llvm.getelementptr %[[QUEUE]][0, 3]
// CHECK: %[[STATUS:.*]] = llvm.load %[[STATUS_PTR]]
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
// CHECK: %[[OK:.*]] = llvm.icmp "eq" %[[STATUS]], %[[ZERO]]
// CHECK-NEXT: llvm.cond_br %[[OK]], ^{{bb[0-9]+}}, ^[[RETURN:bb[0-9]+]](%[[STATUS]] : i32)
// CHECK: ^[[RETURN]](%[[RESULT:.*]]: i32):
// CHECK-NEXT: %[[BOUNDARY:.*]] = llvm.call @__obelisk_eval_route_promotion_boundary_v1(%[[RESULT]])
// CHECK-NEXT: llvm.return %[[BOUNDARY]]
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_0(
// CHECK: %[[INSIDE:.*]] = llvm.icmp "ult"
// CHECK: %[[QUEUE:.*]] = llvm.mlir.addressof @__obelisk_eval_ordered_nba_queue_v1
// CHECK: %[[STATUS_PTR:.*]] = llvm.getelementptr %[[QUEUE]][0, 3]
// CHECK: %[[STATUS:.*]] = llvm.load %[[STATUS_PTR]]
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
// CHECK: %[[OK:.*]] = llvm.icmp "eq" %[[STATUS]], %[[ZERO]]
// CHECK: %[[PROCEED:.*]] = llvm.and %[[INSIDE]], %[[OK]] : i1
// CHECK-NEXT: llvm.cond_br %[[PROCEED]], ^[[LOAD:bb[0-9]+]], ^[[DONE:bb[0-9]+]]
// CHECK: ^[[LOAD]]:
// CHECK: llvm.load
// CHECK: "llvm.intr.cttz"
// CHECK: ^[[DONE]]:
// CHECK-NEXT: %[[SUCCESS:.*]] = llvm.mlir.constant(0 : i32)
// CHECK-NEXT: llvm.br ^{{bb[0-9]+}}(%[[SUCCESS]] : i32)
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_1(
// CHECK: %[[INSIDE:.*]] = llvm.icmp "ult"
// CHECK: %[[QUEUE:.*]] = llvm.mlir.addressof @__obelisk_eval_ordered_nba_queue_v1
// CHECK: %[[STATUS_PTR:.*]] = llvm.getelementptr %[[QUEUE]][0, 3]
// CHECK: %[[STATUS:.*]] = llvm.load %[[STATUS_PTR]]
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
// CHECK: %[[OK:.*]] = llvm.icmp "eq" %[[STATUS]], %[[ZERO]]
// CHECK: %[[PROCEED:.*]] = llvm.and %[[INSIDE]], %[[OK]] : i1
// CHECK-NEXT: llvm.cond_br %[[PROCEED]], ^[[LOAD:bb[0-9]+]], ^[[DONE:bb[0-9]+]]
// CHECK: ^[[LOAD]]:
// CHECK: llvm.load
// CHECK: "llvm.intr.cttz"
// CHECK: ^[[DONE]]:
// CHECK-NEXT: %[[SUCCESS:.*]] = llvm.mlir.constant(0 : i32)
// CHECK-NEXT: llvm.br ^{{bb[0-9]+}}(%[[SUCCESS]] : i32)

!int = !simulation.ref<i32>
!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
!wide = !simulation.logic<65>
!wref = !simulation.ref<!wide>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  simulation.design @ordered {
    simulation.scope.decl 0
    simulation.storage.decl 5 in 0 : i32 design {test.replicate = 513 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.code_unit.decl 8 in 0 always hierarchy "ordered.counter#" {test.replicate = 513 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !wide design
    simulation.storage.decl 4 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "ordered.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "ordered.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "ordered.forward"
    simulation.code_unit.decl 4 in 0 always hierarchy "ordered.writer"
    simulation.code_unit.decl 5 in 0 always hierarchy "ordered.reset"
    simulation.code_unit.decl 6 in 0 always hierarchy "ordered.edge"
    simulation.code_unit.decl 7 in 0 initial hierarchy "ordered.check"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %src = simulation.context.storage %ctx[1] : !ref
      %kick = simulation.context.storage %ctx[2] : !ref
      %data = simulation.context.storage %ctx[3] : !wref
      %seen = simulation.context.storage %ctx[4] : !ref
      %zero = simulation.logic.constant false, false : !bit
      %zeros = simulation.logic.constant 0 : i65, 0 : i65 : !wide
      simulation.ref.store %zero to %clk : !bit, !ref
      simulation.ref.store %zero to %src : !bit, !ref
      simulation.ref.store %zero to %kick : !bit, !ref
      simulation.ref.store %zeros to %data : !wide, !wref
      simulation.ref.store %zero to %seen : !bit, !ref
      %a = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %b = simulation.spawn @forward(%ctx, %clk, %src) : !simulation.context, !ref, !ref -> !simulation.process
      %c = simulation.spawn @writer(%ctx, %src, %kick, %data) : !simulation.context, !ref, !ref, !wref -> !simulation.process
      %d = simulation.spawn @reset(%ctx, %kick, %src) : !simulation.context, !ref, !ref -> !simulation.process
      %e = simulation.spawn @edge(%ctx, %data, %seen) : !simulation.context, !wref, !ref -> !simulation.process
      %f = simulation.spawn @check(%ctx, %data, %seen) : !simulation.context, !wref, !ref -> !simulation.process
      %n = simulation.context.storage %ctx[5] {test.replicate = 513 : i64, test.replicate_stride = {id = 1 : i64}} : !int
      %k = simulation.spawn @"counter#"(%ctx, %clk, %n) {test.replicate = 513 : i64} : !simulation.context, !ref, !int -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !ref -> !bit
      %new = simulation.logic.unary bit_not %old : (!bit) -> !bit
      simulation.ref.store %new to %clk : !bit, !ref
      cf.br ^wait
    }
    simulation.func @forward(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^copy {site = #schedule.continuation<id = 2>} : !ref
    ^copy:
      %value = simulation.ref.load %clk : !ref -> !bit
      simulation.ref.store %value to %src : !bit, !ref
      cf.br ^wait
    }
    simulation.func @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %kick: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %src to ^write {site = #schedule.continuation<id = 3>} : !ref
    ^write:
      %value = simulation.ref.load %src : !ref -> !bit
      %target = simulation.ref.extract %data from 0 : !wref -> !ref
      simulation.nba.enqueue %value to %target : (!bit, !ref) -> ()
      simulation.ref.store %value to %kick : !bit, !ref
      cf.br ^wait
    }
    simulation.func @reset(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %kick: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.any %kick edges [1] to ^reset {site = #schedule.continuation<id = 4>} : !ref
    ^reset:
      %zero = simulation.logic.constant false, false : !bit
      simulation.ref.store %zero to %src : !bit, !ref
      cf.br ^wait
    }
    simulation.func @edge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %seen: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.any %data edges [1] to ^edge {site = #schedule.continuation<id = 5>} : !wref
    ^edge:
      %one = simulation.logic.constant true, false : !bit
      simulation.ref.store %one to %seen : !bit, !ref
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %seen: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^check {site = #schedule.continuation<id = 6>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^check:
      %value = simulation.ref.load %data : !wref -> !wide
      %edge = simulation.ref.load %seen : !ref -> !bit
      %format = simulation.bytes.constant "%017h %b"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %value, %edge) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !wide, !bit
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
    simulation.func @"counter#"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %n: !int {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64, test.replicate_stride = {simulation.descriptor_id = 1 : i64}}) attributes {entry_kind = 3 : i32, code_unit_id = 8 : i64, test.replicate = 513 : i64, test.replicate_stride = {code_unit_id = 1 : i64}} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 2>} : !ref
    ^body:
      %old = simulation.ref.load %n : !int -> i32
      %one = arith.constant 1 : i32
      %new = arith.addi %old, %one : i32
      simulation.ref.store %new to %n : i32, !int
      %three = arith.constant 3 : i32
      %done = arith.cmpi eq, %new, %three : i32
      cf.cond_br %done, ^finish, ^wait
    ^finish:
      %status = arith.constant 1 : i32
      simulation.finish %ctx, %status
      cf.br ^wait
    }
  }
}
