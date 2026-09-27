// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' \
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
  simulation.design @eval_dynamic_nba {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_dynamic_nba.root"
    simulation.code_unit.decl 2 in 0 always
        hierarchy "eval_dynamic_nba.clock"
    simulation.code_unit.decl 3 in 0 always
        hierarchy "eval_dynamic_nba.update"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %data = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<32>>
      %index = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<64>>
      %c = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %p = simulation.spawn @update(%ctx, %clock, %data, %index) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<32>>,
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
        %data: !simulation.ref<!simulation.logic<32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %index: !simulation.ref<!simulation.logic<64>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume
          {site = #schedule.continuation<id = 2>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %low = simulation.ref.load %index :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %base = simulation.ref.extract %data from 8 :
          !simulation.ref<!simulation.logic<32>> ->
          !simulation.ref<!simulation.logic<16>>
      %slice = simulation.ref.dyn_extract %base from %low :
          (!simulation.ref<!simulation.logic<16>>,
           !simulation.logic<64>) -> !simulation.ref<!simulation.logic<8>>
      %value = simulation.logic.constant 165 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %value to %slice :
          (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
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
