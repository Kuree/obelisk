// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --implicit-check-not=simulation.nba.enqueue
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=DIRECT --implicit-check-not='llvm.call @obelisk_rt_v1_scheduler_static_transition'
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=DIRECT
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=PERIODIC
// RUN: sed 's/schedule.native_scheduler = 2/schedule.native_scheduler = 3/' %s \
// RUN:   | obelisk-opt - \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=TWO-STATE
// RUN: sed -e 's/schedule.native_scheduler = 2/schedule.native_scheduler = 3/' \
// RUN:   -e 's/attributes {entry_kind = 1 : i32/attributes {schedule.eval.inductive_two_state, entry_kind = 1 : i32/' %s \
// RUN:   | obelisk-opt - \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=TWO-STATE-STAGE
// RUN: sed -e 's/schedule.native_scheduler = 2/schedule.native_scheduler = 3/' \
// RUN:   -e 's/attributes {entry_kind = 1 : i32/attributes {schedule.eval.selected_two_state, entry_kind = 1 : i32/' %s \
// RUN:   | obelisk-opt - \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=SELECTED-STAGE
// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=PERIODIC-ANALYSIS

// PERIODIC-ANALYSIS: native-aot eligible=true fully=true selected=true periodic=true
// Exercise AOT NBA planning and materialization from hand-authored simulation
// IR. Driver option parsing is deliberately outside this pass test.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 2 : i32
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
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
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

// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_aot_nba_roots_v1
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_aot_nba_sites_v1
// CHECK-DAG: llvm.mlir.global internal @__obelisk_aot_nba_accumulator_0
// CHECK-DAG: llvm.mlir.global internal @__obelisk_aot_nba_dirty_roots_v1
// CHECK-DAG: llvm.mlir.global internal @__obelisk_aot_nba_dirty_summary_v1
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_periodic_clock_plan_v1
// CHECK-DAG: llvm.func @__obelisk_aot_static_nba_commit_v1
// CHECK-DAG: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// CHECK-DAG: llvm.store
// CHECK-DAG: llvm.call @obelisk_rt_v1_static_nba_commit_roots
// CHECK-DAG: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
// CHECK-NOT: llvm.call @obelisk_rt_v1_static_nba_claim

// Structural periodic-clock planning is independent of symbol names and sorts
// the physical clocks by half-period. The fast clock (2) must precede the slow
// clock (3), even though the root spawns slow first.
// PERIODIC-LABEL: llvm.mlir.global internal constant @__obelisk_periodic_clock_plan_v1
// PERIODIC: llvm.mlir.constant(2 : i64) : i64
// PERIODIC: llvm.insertvalue {{.*}}[5]
// PERIODIC: llvm.insertvalue {{.*}}[0]
// PERIODIC: llvm.mlir.constant(3 : i64) : i64
// PERIODIC: llvm.insertvalue {{.*}}[5]
// PERIODIC: llvm.insertvalue {{.*}}[1]

// DIRECT: llvm.func @__obelisk_aot_static_nba_commit_v1
// Writable capability alone must not remove the generated scalar barrier.
// The runtime guard below rejects dirty roots, live observers, and handoffs.
// DIRECT: llvm.call @obelisk_rt_v1_static_nba_direct_commit_guard
// DIRECT: llvm.mlir.addressof @__obelisk_state_value
// DIRECT: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// DIRECT: llvm.load
// DIRECT: llvm.store
// DIRECT: llvm.select
// DIRECT: llvm.call @obelisk_rt_v1_scheduler_activate_static_nodes
// DIRECT: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// DIRECT: llvm.xor
// DIRECT: llvm.and
// DIRECT: llvm.store
// DIRECT: llvm.call @obelisk_rt_v1_static_nba_account_generated_commits
// DIRECT: llvm.call @obelisk_rt_v1_static_nba_commit_roots

// A stale four-state staged unknown plane must not leak through promotion.
// The transitional two-state barrier clears canonical unknown bits once; the
// steady fast clone has no canonical unknown-plane memory access.  Both use a
// zero staged unknown value.
// NBA specialization follows call-site intent across generated coordinator
// variants; lowering consumes the marker attributes instead of keying on
// coordinator symbol names.
// TWO-STATE-NOT: schedule.eval.use_fast_two_state_nba
// TWO-STATE-NOT: schedule.eval.use_canonical_two_state_nba
// Generated spawns are context-bound, so process construction reuses the
// immutable design image validated by context creation instead of reparsing
// it once per process.
// TWO-STATE-DAG: llvm.func @obelisk_rt_v1_process_spawn(!llvm.ptr, !llvm.ptr, !llvm.ptr) -> i64
// TWO-STATE-LABEL: llvm.func @root.__obelisk_spawn(
// TWO-STATE-SAME: %[[SPAWN_CTX:.*]]: !llvm.ptr)
// TWO-STATE: llvm.call @obelisk_rt_v1_process_spawn(%[[SPAWN_CTX]], {{.*}}, {{.*}})
// TWO-STATE-LABEL: llvm.func @__obelisk_eval_dispatch_v1
// TWO-STATE-SAME: alignment = 64 : i64
// TWO-STATE: %[[DIRTY_ROOTS:.*]] = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// TWO-STATE: llvm.load {{.*}} : !llvm.ptr -> i64
// TWO-STATE: llvm.icmp "ne" {{.*}} : i64
// TWO-STATE: llvm.cond_br
// TWO-STATE: llvm.call @__obelisk_aot_static_nba_commit_two_state_fast_v1
// TWO-STATE: llvm.call @__obelisk_aot_static_nba_commit_two_state_v1
// TWO-STATE-LABEL: llvm.func internal @__obelisk_aot_static_nba_commit_two_state_v1
// TWO-STATE: %[[CANON_UNKNOWN:.*]] = llvm.mlir.addressof @__obelisk_state_unknown
// TWO-STATE: %[[CANON_ACC:.*]] = llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// TWO-STATE: llvm.getelementptr %[[CANON_ACC]][32]
// TWO-STATE-NEXT: {{.*}} = llvm.mlir.zero : i64
// TWO-STATE: llvm.getelementptr %[[CANON_UNKNOWN]]
// TWO-STATE-LABEL: llvm.func internal @__obelisk_aot_static_nba_commit_two_state_fast_v1
// TWO-STATE-SAME: alignment = 128 : i64
// The production materializer specializes the clone to the steady two-state
// region and accesses an aligned byte root at its declared width. This is an
// actual pass-pipeline check: widening it back to the packed 64-bit fallback
// would reintroduce the adjacent-byte read/modify/write on the hot path.
// TWO-STATE: llvm.mlir.constant(2 : i32) : i32
// TWO-STATE: %[[FAST_UNKNOWN:.*]] = llvm.mlir.addressof @__obelisk_state_unknown
// TWO-STATE: %[[FAST_ACC:.*]] = llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// TWO-STATE: llvm.getelementptr %[[FAST_ACC]][32]
// TWO-STATE-NEXT: {{.*}} = llvm.mlir.zero : i64
// TWO-STATE: %[[FAST_VALUE_ADDR:.*]] = llvm.getelementptr {{.*}}[0]
// TWO-STATE-NEXT: %[[FAST_OLD8:.*]] = llvm.load %[[FAST_VALUE_ADDR]] {{.*}} : !llvm.ptr -> i8
// TWO-STATE: %[[FAST_NEW8:.*]] = llvm.trunc {{.*}} : i64 to i8
// TWO-STATE: llvm.store %[[FAST_NEW8]], {{.*}} {{.*}} : i8, !llvm.ptr
// The independent one-bit NBA root is byte-aligned by the production state
// layout and uses an exact i8 access too. This checks the actual sub-byte
// transformation rather than merely an already byte-wide source declaration.
// TWO-STATE: %[[BIT_ACC:.*]] = llvm.mlir.addressof @__obelisk_aot_nba_accumulator_1
// TWO-STATE: %[[BIT_OLD8:.*]] = llvm.load {{.*}} {{.*}} : !llvm.ptr -> i8
// TWO-STATE: %[[BIT_NEW8:.*]] = llvm.trunc {{.*}} : i64 to i8
// TWO-STATE: llvm.store %[[BIT_NEW8]], {{.*}} {{.*}} : i8, !llvm.ptr
// TWO-STATE: %[[FAST_UNKNOWN_ADDR:.*]] = llvm.getelementptr %[[FAST_UNKNOWN]]
// TWO-STATE-NOT: llvm.load %[[FAST_UNKNOWN_ADDR]]
// TWO-STATE-NOT: llvm.store {{.*}}, %[[FAST_UNKNOWN_ADDR]]
// TWO-STATE: llvm.return

// State-domain analysis derives a precise proof for this NBA's stored value
// and exact destination root. Its staged write uses the dirty bit as validity
// and can omit write-mask, region, valid, and summary traffic. It must still
// clear a stale unknown lane left by a coincident four-state writer in the
// shared accumulator.
// TWO-STATE-STAGE-LABEL: llvm.func @process(
// TWO-STATE-STAGE: %[[ACC:.*]] = llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// TWO-STATE-STAGE: %[[VALUE:.*]] = llvm.getelementptr %[[ACC]][0]
// TWO-STATE-STAGE: llvm.store {{.*}}, %[[VALUE]]
// TWO-STATE-STAGE: %[[STAGE_UNKNOWN:.*]] = llvm.getelementptr %[[ACC]][32]
// TWO-STATE-STAGE: %[[STAGE_ZERO:.*]] = llvm.mlir.constant(0 : i64)
// TWO-STATE-STAGE: llvm.store %[[STAGE_ZERO]], %[[STAGE_UNKNOWN]]
// TWO-STATE-STAGE-NOT: llvm.getelementptr %[[ACC]][64]
// TWO-STATE-STAGE-NOT: llvm.getelementptr %[[ACC]][96]
// TWO-STATE-STAGE-NOT: llvm.getelementptr %[[ACC]][100]
// TWO-STATE-STAGE: %[[DIRTY:.*]] = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// TWO-STATE-STAGE: %[[DIRTY_WORD:.*]] = llvm.getelementptr %[[DIRTY]][0]
// TWO-STATE-STAGE: llvm.store {{.*}}, %[[DIRTY_WORD]]
// TWO-STATE-STAGE-NOT: llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1
// TWO-STATE-STAGE-LABEL: llvm.func @process.__obelisk_native_requirements

// A selected two-state body may use fixed value/mask/region metadata, but a
// coincident four-state owner can have staged X in the shared accumulator.
// Without the stronger per-access root proof, explicitly overwrite the
// staged unknown field with zero to preserve last-writer semantics.
// SELECTED-STAGE-LABEL: llvm.func @process(
// SELECTED-STAGE: llvm.mlir.constant(0 : i8)
// SELECTED-STAGE: %[[ZERO8:.*]] = llvm.mlir.constant(0 : i8)
// SELECTED-STAGE: %[[ZERO64:.*]] = llvm.zext %[[ZERO8]] : i8 to i64
// SELECTED-STAGE: %[[SELECTED_ACC:.*]] = llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// SELECTED-STAGE: %[[SELECTED_VALUE:.*]] = llvm.getelementptr %[[SELECTED_ACC]][0]
// SELECTED-STAGE: llvm.store {{.*}}, %[[SELECTED_VALUE]]
// SELECTED-STAGE: %[[SELECTED_UNKNOWN:.*]] = llvm.getelementptr %[[SELECTED_ACC]][32]
// SELECTED-STAGE: %[[MASKED_ZERO:.*]] = llvm.and %[[ZERO64]], {{.*}} : i64
// SELECTED-STAGE: llvm.store %[[MASKED_ZERO]], %[[SELECTED_UNKNOWN]]
// SELECTED-STAGE-NOT: llvm.getelementptr %[[SELECTED_ACC]][64]
// SELECTED-STAGE-NOT: llvm.getelementptr %[[SELECTED_ACC]][96]
// SELECTED-STAGE-NOT: llvm.getelementptr %[[SELECTED_ACC]][100]
// SELECTED-STAGE: %[[SELECTED_DIRTY:.*]] = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// SELECTED-STAGE: %[[SELECTED_DIRTY_WORD:.*]] = llvm.getelementptr %[[SELECTED_DIRTY]][0]
// SELECTED-STAGE: llvm.store {{.*}}, %[[SELECTED_DIRTY_WORD]]
// SELECTED-STAGE-NOT: llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1
// SELECTED-STAGE-LABEL: llvm.func @process.__obelisk_native_requirements
