// RUN: talos-opt %s --split-input-file \
// RUN:   --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// Eval-scheduled variants of simulation-to-llvm-coroutine-aot-nba.mlir whose
// NBA writer carries a two-state proof.

// State-domain analysis derives a precise proof for this NBA's stored value
// and exact destination root. Its staged write uses the dirty bit as validity
// and can omit write-mask, region, valid, and summary traffic. It must still
// clear a stale unknown lane left by a coincident four-state writer in the
// shared accumulator.
// CHECK-LABEL: module @inductive
// CHECK-LABEL: llvm.func @process(
// CHECK: %[[ACC:.*]] = llvm.mlir.addressof @__talos_aot_nba_accumulator_0
// CHECK: %[[VALUE:.*]] = llvm.getelementptr %[[ACC]][0]
// CHECK: llvm.store {{.*}}, %[[VALUE]]
// CHECK: %[[STAGE_UNKNOWN:.*]] = llvm.getelementptr %[[ACC]][32]
// CHECK: %[[STAGE_ZERO:.*]] = llvm.mlir.constant(0 : i64)
// CHECK: llvm.store %[[STAGE_ZERO]], %[[STAGE_UNKNOWN]]
// CHECK-NOT: llvm.getelementptr %[[ACC]][64]
// CHECK-NOT: llvm.getelementptr %[[ACC]][96]
// CHECK-NOT: llvm.getelementptr %[[ACC]][100]
// CHECK: %[[DIRTY:.*]] = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1
// CHECK: %[[DIRTY_WORD:.*]] = llvm.getelementptr %[[DIRTY]][0]
// CHECK: llvm.store {{.*}}, %[[DIRTY_WORD]]
// CHECK-NOT: llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1
// CHECK-LABEL: llvm.func @process.__talos_native_execute

module @inductive attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @aot_nba {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "aot_nba.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "aot_nba.process"
    simulation.code_unit.decl 3 in 0 always hierarchy "aot_nba.watcher"
    simulation.code_unit.decl 4 in 0 always hierarchy "aot_nba.clock_slow"
    simulation.code_unit.decl 5 in 0 always hierarchy "aot_nba.clock_fast"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    // Keep an eight-byte addressable tail after the scalar root. Direct
    // generated commits use one unaligned 64-bit word plus an optional ninth
    // byte and leave a boundary root on the validating generic path.
    simulation.storage.decl 1 in 0 : !simulation.logic<64> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %storage = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<8>>
      %clock_slow = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<1>>
      %clock_fast = simulation.context.storage %ctx[3] :
          !simulation.ref<!simulation.logic<1>>
      %process = simulation.spawn @process(%ctx, %storage, %clock_slow) :
          !simulation.context, !simulation.ref<!simulation.logic<8>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %watcher = simulation.spawn @watcher(%ctx, %storage) :
          !simulation.context, !simulation.ref<!simulation.logic<8>>
          -> !simulation.process
      %slow = simulation.spawn @clock_slow(%ctx, %clock_slow) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %fast = simulation.spawn @clock_fast(%ctx, %clock_fast) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      simulation.return
    }

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %bit_destination: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {schedule.eval.inductive_two_state, entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.logic.constant 42 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %value to %destination :
          (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      %bit_value = simulation.logic.constant 1 : i1, 0 : i1 :
          !simulation.logic<1>
      simulation.nba.enqueue %bit_value to %bit_destination :
          (!simulation.logic<1>,
           !simulation.ref<!simulation.logic<1>>) -> ()
      simulation.return
    }

    simulation.func @watcher(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %source to ^resume
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<8>>
    ^resume:
      cf.br ^wait
    }

    simulation.func @clock_slow(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 2>,
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

    simulation.func @clock_fast(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 3>,
           timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
  }
}


// -----

// A selected two-state body may use fixed value/mask/region metadata, but a
// coincident four-state owner can have staged X in the shared accumulator.
// Without the stronger per-access root proof, explicitly overwrite the
// staged unknown field with zero to preserve last-writer semantics.
// CHECK-LABEL: module @selected
// CHECK-LABEL: llvm.func @process(
// CHECK: llvm.mlir.constant(0 : i8)
// CHECK: %[[ZERO8:.*]] = llvm.mlir.constant(0 : i8)
// CHECK: %[[ZERO64:.*]] = llvm.zext %[[ZERO8]] : i8 to i64
// CHECK: %[[SELECTED_ACC:.*]] = llvm.mlir.addressof @__talos_aot_nba_accumulator_0
// CHECK: %[[SELECTED_VALUE:.*]] = llvm.getelementptr %[[SELECTED_ACC]][0]
// CHECK: llvm.store {{.*}}, %[[SELECTED_VALUE]]
// CHECK: %[[SELECTED_UNKNOWN:.*]] = llvm.getelementptr %[[SELECTED_ACC]][32]
// CHECK: %[[MASKED_ZERO:.*]] = llvm.and %[[ZERO64]], {{.*}} : i64
// CHECK: llvm.store %[[MASKED_ZERO]], %[[SELECTED_UNKNOWN]]
// CHECK-NOT: llvm.getelementptr %[[SELECTED_ACC]][64]
// CHECK-NOT: llvm.getelementptr %[[SELECTED_ACC]][96]
// CHECK-NOT: llvm.getelementptr %[[SELECTED_ACC]][100]
// CHECK: %[[SELECTED_DIRTY:.*]] = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1
// CHECK: %[[SELECTED_DIRTY_WORD:.*]] = llvm.getelementptr %[[SELECTED_DIRTY]][0]
// CHECK: llvm.store {{.*}}, %[[SELECTED_DIRTY_WORD]]
// CHECK-NOT: llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1
// CHECK-LABEL: llvm.func @process.__talos_native_execute

module @selected attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @aot_nba {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "aot_nba.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "aot_nba.process"
    simulation.code_unit.decl 3 in 0 always hierarchy "aot_nba.watcher"
    simulation.code_unit.decl 4 in 0 always hierarchy "aot_nba.clock_slow"
    simulation.code_unit.decl 5 in 0 always hierarchy "aot_nba.clock_fast"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    // Keep an eight-byte addressable tail after the scalar root. Direct
    // generated commits use one unaligned 64-bit word plus an optional ninth
    // byte and leave a boundary root on the validating generic path.
    simulation.storage.decl 1 in 0 : !simulation.logic<64> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %storage = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<8>>
      %clock_slow = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<1>>
      %clock_fast = simulation.context.storage %ctx[3] :
          !simulation.ref<!simulation.logic<1>>
      %process = simulation.spawn @process(%ctx, %storage, %clock_slow) :
          !simulation.context, !simulation.ref<!simulation.logic<8>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %watcher = simulation.spawn @watcher(%ctx, %storage) :
          !simulation.context, !simulation.ref<!simulation.logic<8>>
          -> !simulation.process
      %slow = simulation.spawn @clock_slow(%ctx, %clock_slow) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %fast = simulation.spawn @clock_fast(%ctx, %clock_fast) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      simulation.return
    }

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %bit_destination: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {schedule.eval.selected_two_state, entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.logic.constant 42 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %value to %destination :
          (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      %bit_value = simulation.logic.constant 1 : i1, 0 : i1 :
          !simulation.logic<1>
      simulation.nba.enqueue %bit_value to %bit_destination :
          (!simulation.logic<1>,
           !simulation.ref<!simulation.logic<1>>) -> ()
      simulation.return
    }

    simulation.func @watcher(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %source to ^resume
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<8>>
    ^resume:
      cf.br ^wait
    }

    simulation.func @clock_slow(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 2>,
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

    simulation.func @clock_fast(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 3>,
           timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
  }
}

