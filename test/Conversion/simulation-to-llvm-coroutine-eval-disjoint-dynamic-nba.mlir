// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' \
// RUN:   -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s
// RUN: FileCheck %s --check-prefix=ORIGIN < %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph))' | FileCheck %s --check-prefix=ORIGIN

// Byte lanes from different semantic NBA sites cannot overlap even when their
// array indices differ. Keep a separate once-per-edge latch for each lane;
// this must not make overlapping or repeated sites eligible.
// The canonical actor and its mutually exclusive eval clone must retain the
// same semantic site IDs even when a later graph rebuild renumbers sites.
// ORIGIN-LABEL: simulation.func @update(
// ORIGIN: simulation.nba.enqueue {{.*}}schedule.eval.origin_nba_site = [[LO:[0-9]+]] : i64
// ORIGIN: simulation.nba.enqueue {{.*}}schedule.eval.origin_nba_site = [[HI:[0-9]+]] : i64
// ORIGIN-LABEL: simulation.func private @update.__obelisk_eval_body_0(
// ORIGIN: simulation.nba.enqueue {{.*}}schedule.eval.origin_nba_site = [[LO]] : i64
// ORIGIN: simulation.nba.enqueue {{.*}}schedule.eval.origin_nba_site = [[HI]] : i64
!words = !simulation.unpacked_array<0 : 31 x !simulation.logic<32>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @eval_disjoint_dynamic_nba {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_disjoint_dynamic_nba.root"
    simulation.code_unit.decl 2 in 0 always
        hierarchy "eval_disjoint_dynamic_nba.clock"
    simulation.code_unit.decl 3 in 0 always
        hierarchy "eval_disjoint_dynamic_nba.update"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !words design
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design
    simulation.storage.decl 3 in 0 : !simulation.logic<64> design

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
