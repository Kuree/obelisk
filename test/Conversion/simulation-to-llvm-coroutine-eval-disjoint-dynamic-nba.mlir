// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' \
// RUN:   -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s
// RUN: FileCheck %s --check-prefix=ORIGIN < %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph))' | FileCheck %s --check-prefix=ORIGIN

// Byte lanes from different semantic NBA sites cannot overlap even when their
// array indices differ. Keep a separate once-per-edge latch for each lane;
// this must not make overlapping or repeated sites eligible.
// The canonical actor and its mutually exclusive eval clone must retain the
// same semantic site IDs even when a later graph rebuild renumbers sites.
// ORIGIN-LABEL: obelisk_sim.func @update(
// ORIGIN: obelisk_sim.nba.enqueue {{.*}}schedule.eval.origin_nba_site = [[LO:[0-9]+]] : i64
// ORIGIN: obelisk_sim.nba.enqueue {{.*}}schedule.eval.origin_nba_site = [[HI:[0-9]+]] : i64
// ORIGIN-LABEL: obelisk_sim.func private @update.__obelisk_eval_body_0(
// ORIGIN: obelisk_sim.nba.enqueue {{.*}}schedule.eval.origin_nba_site = [[LO]] : i64
// ORIGIN: obelisk_sim.nba.enqueue {{.*}}schedule.eval.origin_nba_site = [[HI]] : i64
!words = !obelisk_sim.unpacked_array<0 : 31 x !obelisk_sim.logic<32>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @eval_disjoint_dynamic_nba {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_disjoint_dynamic_nba.root"
    obelisk_sim.code_unit.decl 2 in 0 always
        hierarchy "eval_disjoint_dynamic_nba.clock"
    obelisk_sim.code_unit.decl 3 in 0 always
        hierarchy "eval_disjoint_dynamic_nba.update"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !words design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<64> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<64> design

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
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
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
          {site = #schedule.continuation<id = 2>} :
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
  }
}

// CHECK-COUNT-2: llvm.mlir.global internal @__obelisk_eval_nba_valid_
// CHECK-LABEL: llvm.func @update.__obelisk_eval_body_0(
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_nba
// CHECK: llvm.mlir.constant(2147483647 : i64)
// CHECK-NEXT: {{.*}} llvm.icmp "sle"
// CHECK: llvm.mlir.constant(-2147483648 : i64)
// CHECK-NEXT: {{.*}} llvm.icmp "sge"
// CHECK: llvm.mlir.addressof @__obelisk_eval_nba_offset_
// CHECK: llvm.mlir.addressof @__obelisk_eval_nba_offset_
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_nba
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// CHECK: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
