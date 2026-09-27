// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' \
// RUN:   -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// Feed preplanned Simulation IR to only the coroutine conversion pass.  The
// eval body bases a dynamic NBA at bit 8 of a wider root.  The pass must merge
// every execution directly into the root accumulator in source order; that
// generated form remains correct when a source-level loop revisits the site,
// whereas a one-entry per-site latch loses legal iterations.  The exact
// generated bounds below lock down the fixed base offset too.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @eval_dynamic_nba {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_dynamic_nba.root"
    obelisk_sim.code_unit.decl 2 in 0 always
        hierarchy "eval_dynamic_nba.clock"
    obelisk_sim.code_unit.decl 3 in 0 always
        hierarchy "eval_dynamic_nba.update"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<64> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %data = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %index = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %c = obelisk_sim.spawn @clock(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %p = obelisk_sim.spawn @update(%ctx, %clock, %data, %index) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<32>>,
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
        %data: !obelisk_sim.ref<!obelisk_sim.logic<32>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %index: !obelisk_sim.ref<!obelisk_sim.logic<64>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume
          {site = #schedule.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %low = obelisk_sim.ref.load %index :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %base = obelisk_sim.ref.extract %data from 8 :
          !obelisk_sim.ref<!obelisk_sim.logic<32>> ->
          !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %slice = obelisk_sim.ref.dyn_extract %base from %low :
          (!obelisk_sim.ref<!obelisk_sim.logic<16>>,
           !obelisk_sim.logic<64>) -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %value = obelisk_sim.logic.constant 165 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %value to %slice :
          (!obelisk_sim.logic<8>,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: llvm.func @update.__obelisk_eval_body_0(
// CHECK: %[[UNKNOWN_BASE:.*]] = llvm.mlir.addressof @__obelisk_state_unknown
// CHECK: %[[UNKNOWN_ADDR:.*]] = llvm.getelementptr %[[UNKNOWN_BASE]]
// CHECK: %[[UNKNOWN:.*]] = llvm.load %[[UNKNOWN_ADDR]]
// CHECK: %[[KNOWN:.*]] = llvm.icmp "eq" %[[UNKNOWN]],
// CHECK: %[[INDEX_VALID:.*]] = llvm.and {{%.*}}, %[[KNOWN]]
// CHECK: llvm.mlir.constant(16 : i64)
// CHECK: llvm.mlir.constant(-8 : i64)
// CHECK: %[[BELOW_END:.*]] = llvm.icmp "slt"
// CHECK: %[[ABOVE_BEGIN:.*]] = llvm.icmp "sgt"
// CHECK: %[[IN_BOUNDS:.*]] = llvm.and %[[BELOW_END]], %[[ABOVE_BEGIN]]
// CHECK: %[[ACTIVE:.*]] = llvm.and %[[INDEX_VALID]], %[[IN_BOUNDS]]
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// CHECK: llvm.store
// CHECK-NOT: __obelisk_eval_nba_offset_
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_nba
// CHECK-NOT: llvm.call @malloc
// CHECK: llvm.return
