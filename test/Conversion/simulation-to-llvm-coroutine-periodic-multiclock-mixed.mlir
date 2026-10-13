// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// The crossing value is a mixed known/unknown constant instead of a load.
// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// CHECK: llvm.call @__obelisk_direct_fragment_3_9.__obelisk_execute(
// CHECK-NOT: llvm.call @__obelisk_direct_fragment_3_9.__obelisk_execute.two_state
// CHECK: %[[PENDING:.*]] = llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// CHECK-NEXT: %[[BITS:.*]] = llvm.load %[[PENDING]] {{.*}} : !llvm.ptr -> i64
// CHECK-NEXT: %[[BIT:.*]] = llvm.mlir.constant(2 : i64)
// CHECK-NEXT: %[[LOCAL:.*]] = llvm.and %[[BITS]], %[[BIT]] : i64
// CHECK: %[[KNOWN:.*]] = llvm.icmp "eq" %[[LOCAL]], {{.*}} : i64
// CHECK-NEXT: llvm.cond_br %[[KNOWN]], ^[[FAST:bb[0-9]+]], ^[[SCAN:bb[0-9]+]]
// CHECK: ^[[SCAN]]:
// CHECK: %[[OWNER:.*]] = llvm.mlir.constant(1 : i64)
// CHECK-NEXT: {{.*}}llvm.call @__obelisk_eval_kernel_promotion_ready_v1(%[[OWNER]])
// CHECK: ^[[FAST]]:
// CHECK-NEXT: {{.*}}llvm.call @__obelisk_direct_fragment_4_12.__obelisk_execute.two_state.__obelisk_trusted

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @multiclock {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock0"
    simulation.code_unit.decl 3 in 0 always hierarchy "clock1"
    simulation.code_unit.decl 4 in 0 always hierarchy "update0"
    simulation.code_unit.decl 5 in 0 always hierarchy "update1"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk0 = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %clock0 = simulation.spawn @clock0(%ctx, %clk0) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %clk1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %clock1 = simulation.spawn @clock1(%ctx, %clk1) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %source0 = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %target0 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %update0 = simulation.spawn @update0(%ctx, %clk0, %source0, %target0) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %source1 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %target1 = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %update1 = simulation.spawn @update1(%ctx, %clk1, %source1, %target1) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      simulation.return
    }

    simulation.func @clock0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @clock1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 2>,
           timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @update0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %source: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
        %target: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64, schedule.eval.body = @body0, schedule.native.region_body} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^sample
          {site = #schedule.continuation<id = 3>} : !simulation.ref<!simulation.logic<1>>
    ^sample:
      %crossing = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      simulation.nba.enqueue %crossing to %target : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }

    simulation.func @update1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %source: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %target: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64, schedule.eval.body = @body1, schedule.native.region_body} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^sample
          {site = #schedule.continuation<id = 4>} : !simulation.ref<!simulation.logic<1>>
    ^sample:
      %value = simulation.ref.load %source : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.nba.enqueue %value to %target : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }

    simulation.code_unit.decl 6 in 0 function hierarchy "body0" {internal}
    simulation.func private @body0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %source: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
        %target: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64,
          schedule.eval.borrowed_captures, schedule.eval.raw_captures,
          schedule.eval.continuation = 9 : i32,
          schedule.eval.source_owners = [#schedule.source_owner<codeUnit = 4 : i64, continuation = 9 : i32>]} {
      %crossing = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      simulation.nba.enqueue %crossing to %target : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.return
    }

    simulation.code_unit.decl 7 in 0 function hierarchy "body1" {internal}
    simulation.func private @body1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %source: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %target: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64,
          schedule.eval.borrowed_captures, schedule.eval.raw_captures,
          schedule.eval.continuation = 12 : i32,
          schedule.eval.source_owners = [#schedule.source_owner<codeUnit = 5 : i64, continuation = 12 : i32>]} {
      %value = simulation.ref.load %source : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.nba.enqueue %value to %target : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.return
    }
  }
}

// Allocation defers to designated collection points from here on.
// The first clock's equality test bypasses its physical update entirely.
// The rising edge of the faster clock (period 6) is the cycle's only
// designated collection point (IEEE 1800-2023 8.29); falling edges, the slower
// clock and slot ends have none.
// The second clock preserves the first clock's ready mask when not due.
// Silent slots bypass the entire dispatcher, after both clock updates.
// Only selected owners contribute to the shared promotion check.

// The mixed variant replaces one domain's sample with X. That owner cannot
// use the shared proof; check the remaining consumer after its predecessor.
