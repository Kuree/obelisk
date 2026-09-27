// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s
// RUN: sed 's/%crossing = obelisk_sim.ref.load .*$/%crossing = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !obelisk_sim.logic<8>/' %s > %t.mixed.mlir
// RUN: obelisk-opt %t.mixed.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=MIXED

// Two independent clocks drive a bidirectional CDC pipeline. Check the
// generated calendar and shared two-state prefix at the MLIR boundary.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @multiclock {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock0"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "clock1"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "update0"
    obelisk_sim.code_unit.decl 5 in 0 always hierarchy "update1"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk0 = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %clock0 = obelisk_sim.spawn @clock0(%ctx, %clk0) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %clk1 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %clock1 = obelisk_sim.spawn @clock1(%ctx, %clk1) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %source0 = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %target0 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %update0 = obelisk_sim.spawn @update0(%ctx, %clk0, %source0, %target0) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      %source1 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %target1 = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %update1 = obelisk_sim.spawn @update1(%ctx, %clk1, %source1, %target1) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @clock0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clock : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clock : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }

    obelisk_sim.func @clock1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 2>,
           timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clock : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clock : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }

    obelisk_sim.func @update0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %target: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64, schedule.eval.body = @body0, schedule.native.region_body} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^sample
          {site = #schedule.continuation<id = 3>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^sample:
      %crossing = obelisk_sim.ref.load %source : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %crossing to %target : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      cf.br ^wait
    }

    obelisk_sim.func @update1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %target: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64, schedule.eval.body = @body1, schedule.native.region_body} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^sample
          {site = #schedule.continuation<id = 4>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^sample:
      %value = obelisk_sim.ref.load %source : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %value to %target : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      cf.br ^wait
    }

    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "body0" {internal}
    obelisk_sim.func private @body0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %target: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64,
          schedule.eval.borrowed_captures, schedule.eval.raw_captures,
          schedule.eval.continuation = 9 : i32,
          schedule.eval.source_owners = [#schedule.source_owner<codeUnit = 4 : i64, continuation = 9 : i32>]} {
      %crossing = obelisk_sim.ref.load %source : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %crossing to %target : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      obelisk_sim.return
    }

    obelisk_sim.code_unit.decl 7 in 0 function hierarchy "body1" {internal}
    obelisk_sim.func private @body1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %target: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64,
          schedule.eval.borrowed_captures, schedule.eval.raw_captures,
          schedule.eval.continuation = 12 : i32,
          schedule.eval.source_owners = [#schedule.source_owner<codeUnit = 5 : i64, continuation = 12 : i32>]} {
      %value = obelisk_sim.ref.load %source : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %value to %target : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.mlir.global internal constant @__obelisk_periodic_clock_plan_v1
// CHECK-SAME: !llvm.array<2 x
// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// CHECK: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// The first clock's equality test bypasses its physical update entirely.
// CHECK: %[[DUE0:.*]] = llvm.icmp "eq" {{.*}} : i64
// CHECK-NEXT: llvm.cond_br %[[DUE0]], ^[[TOGGLE0:bb[0-9]+]], ^[[NEXT0:bb[0-9]+]]({{.*}} : i1, i64)
// CHECK: ^[[TOGGLE0]]:
// CHECK: llvm.xor {{.*}} : i8
// CHECK: llvm.store {{.*}} : i8, !llvm.ptr
// CHECK: llvm.br ^[[NEXT0]]({{.*}} : i1, i64)
// The second clock preserves the first clock's ready mask when not due.
// CHECK: ^[[NEXT0]](%[[INGRESS0:.*]]: i1, %[[READY0:.*]]: i64):
// CHECK: %[[DUE1:.*]] = llvm.icmp "eq" {{.*}} : i64
// CHECK-NEXT: llvm.cond_br %[[DUE1]], ^[[TOGGLE1:bb[0-9]+]], ^[[NEXT1:bb[0-9]+]](%[[INGRESS0]], %[[READY0]] : i1, i64)
// CHECK: ^[[TOGGLE1]]:
// CHECK: llvm.xor {{.*}} : i8
// CHECK: llvm.store {{.*}} : i8, !llvm.ptr
// CHECK: llvm.or %[[READY0]], {{.*}} : i64
// CHECK: ^[[NEXT1]](%[[INGRESS:.*]]: i1, %[[READY:.*]]: i64):
// Silent slots bypass the entire dispatcher, after both clock updates.
// CHECK-NEXT: llvm.cond_br %[[INGRESS]], ^{{bb[0-9]+}}, ^{{bb[0-9]+}}
// Only selected owners contribute to the shared promotion check.
// CHECK: %[[PENDING_ADDR:.*]] = llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// CHECK: %[[OWNERS:.*]] = llvm.mlir.constant(3 : i64)
// CHECK-NEXT: %[[SELECTED:.*]] = llvm.and %[[READY]], %[[OWNERS]] : i64
// CHECK-NEXT: %[[PENDING:.*]] = llvm.load %[[PENDING_ADDR]] {{.*}} : !llvm.ptr -> i64
// CHECK-NEXT: %[[LOCAL:.*]] = llvm.and %[[SELECTED]], %[[PENDING]] : i64
// CHECK: %[[PROMOTED:.*]] = llvm.icmp "eq" %[[LOCAL]], {{.*}} : i64
// CHECK-NEXT: llvm.cond_br %[[PROMOTED]], ^[[FAST:bb[0-9]+]], ^[[COLD:bb[0-9]+]]
// CHECK: ^[[FAST]]:
// CHECK: llvm.and %[[READY]], {{.*}} : i64
// CHECK: llvm.cond_br {{.*}}, ^[[EXECUTE:bb[0-9]+]], ^{{bb[0-9]+}}
// CHECK: ^[[EXECUTE]]:
// CHECK-NEXT: {{.*}}llvm.call @__obelisk_direct_fragment_3_9.__obelisk_execute.two_state.__obelisk_trusted
// CHECK-NOT: llvm.call @__obelisk_eval_kernel_promotion_ready
// CHECK: llvm.call @__obelisk_direct_fragment_4_12.__obelisk_execute.two_state.__obelisk_trusted
// CHECK: llvm.call @__obelisk_eval_kernel_promotion_ready_v1_0

// The mixed variant replaces one domain's sample with X. That owner cannot
// use the shared proof; check the remaining consumer after its predecessor.
// MIXED-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// MIXED: llvm.call @__obelisk_direct_fragment_3_9.__obelisk_execute(
// MIXED-NOT: llvm.call @__obelisk_direct_fragment_3_9.__obelisk_execute.two_state
// MIXED: %[[PENDING:.*]] = llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// MIXED-NEXT: %[[BITS:.*]] = llvm.load %[[PENDING]] {{.*}} : !llvm.ptr -> i64
// MIXED-NEXT: %[[BIT:.*]] = llvm.mlir.constant(2 : i64)
// MIXED-NEXT: %[[LOCAL:.*]] = llvm.and %[[BITS]], %[[BIT]] : i64
// MIXED: %[[KNOWN:.*]] = llvm.icmp "eq" %[[LOCAL]], {{.*}} : i64
// MIXED-NEXT: llvm.cond_br %[[KNOWN]], ^[[FAST:bb[0-9]+]], ^[[SCAN:bb[0-9]+]]
// MIXED: ^[[SCAN]]:
// MIXED-NEXT: {{.*}}llvm.call @__obelisk_eval_kernel_promotion_ready_v1_1()
// MIXED: ^[[FAST]]:
// MIXED-NEXT: {{.*}}llvm.call @__obelisk_direct_fragment_4_12.__obelisk_execute.two_state.__obelisk_trusted
