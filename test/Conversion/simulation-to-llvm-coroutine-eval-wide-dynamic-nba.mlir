// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' \
// RUN:   -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   -o %t.llvm.mlir
// RUN: FileCheck %s < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PROOF < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=STAGE < %t.llvm.mlir

// Feed preplanned Simulation IR to only the coroutine conversion pass.  A
// provably once-per-periodic-activation dynamic NBA into a wide root uses the
// generated scalar latch.  This locks down the performance-critical pass
// transformation without depending on an end-to-end SystemVerilog design.
!words = !obelisk_sim.unpacked_array<0 : 31 x !obelisk_sim.logic<32>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @eval_wide_dynamic_nba {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_wide_dynamic_nba.root"
    obelisk_sim.code_unit.decl 2 in 0 always
        hierarchy "eval_wide_dynamic_nba.clock"
    obelisk_sim.code_unit.decl 3 in 0 always
        hierarchy "eval_wide_dynamic_nba.update"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !words design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<64> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %data = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!words>
      %index = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %c = obelisk_sim.spawn @clock(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %p = obelisk_sim.spawn @update(%ctx, %clock, %data, %index) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!words>,
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
          {site = #obelisk_sim.continuation<id = 1>,
           timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
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
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume
          {site = #obelisk_sim.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %low = obelisk_sim.ref.load %index :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %element = obelisk_sim.ref.array_element %data[%low] :
          (!obelisk_sim.ref<!words>, !obelisk_sim.logic<64>) ->
          !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %value = obelisk_sim.logic.constant -1515870811 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      obelisk_sim.nba.enqueue %value to %element :
          (!obelisk_sim.logic<32>,
           !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      cf.br ^wait
    }
  }
}

// CHECK: llvm.mlir.global internal @__obelisk_eval_nba_valid_{{[0-9]+}}
// CHECK: llvm.mlir.global internal @__obelisk_eval_nba_unknown_{{[0-9]+}}
// CHECK: llvm.mlir.global internal @__obelisk_eval_nba_value_{{[0-9]+}}
// CHECK: llvm.mlir.global internal @__obelisk_eval_nba_offset_{{[0-9]+}}
// CHECK-LABEL: llvm.func @update.__obelisk_eval_body_0(
// CHECK: llvm.mlir.addressof @__obelisk_eval_nba_offset_{{[0-9]+}}
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @__obelisk_eval_nba_value_{{[0-9]+}}
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @__obelisk_eval_nba_unknown_{{[0-9]+}}
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @__obelisk_eval_nba_valid_{{[0-9]+}}
// CHECK: llvm.store
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_nba
// CHECK-NOT: llvm.call @malloc
// CHECK: llvm.return

// A known payload still has to clear its selected unknown destination. This
// wide dynamic-only root has no cached value-domain certificate: all three
// commits preserve their canonical stores, but its verified clipped footprint
// is disjoint from the index and needs no proof-publication guards.
// PROOF-LABEL: llvm.func @__obelisk_aot_static_nba_commit_v1(
// PROOF-NOT: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// PROOF: llvm.mlir.addressof @__obelisk_state_unknown
// PROOF: llvm.store
// PROOF-NOT: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// PROOF-LABEL: llvm.func internal @__obelisk_aot_static_nba_commit_two_state_v1(
// PROOF-NOT: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// PROOF: llvm.mlir.addressof @__obelisk_state_unknown
// PROOF: llvm.store
// PROOF-NOT: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// PROOF-LABEL: llvm.func internal @__obelisk_aot_static_nba_commit_two_state_fast_v1(
// PROOF-NOT: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// PROOF: llvm.mlir.addressof @__obelisk_state_unknown
// PROOF: llvm.store
// PROOF-NOT: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// PROOF: llvm.func
// STAGE-LABEL: llvm.func @update.__obelisk_eval_body_0(
// STAGE-NOT: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// STAGE: llvm.return
