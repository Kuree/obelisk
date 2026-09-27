// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.llvm.mlir
// RUN: FileCheck %s < %t.llvm.mlir

// Clock instrumentation must survive both explicit and compressed edges.
// Predicates must not count lines, while the selected body and cold callback
// each count their own executed statements exactly once.
// RUN: FileCheck %s --check-prefix=CLOCK < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=BODY < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=BOOTSTRAP < %t.llvm.mlir
// BOOTSTRAP-LABEL: llvm.mlir.global internal constant @__obelisk_periodic_clock_plan_v1
// BOOTSTRAP: llvm.mlir.addressof @__obelisk_periodic_clock_coverage_0
// BOOTSTRAP: llvm.insertvalue {{.*}}[6]
// BOOTSTRAP: llvm.mlir.constant(2 : i64)
// BOOTSTRAP: llvm.insertvalue {{.*}}[7]
// BOOTSTRAP-LABEL: llvm.mlir.global internal constant @__obelisk_periodic_clock_coverage_0
// BOOTSTRAP: llvm.mlir.constant(0 : i64)
// BOOTSTRAP: llvm.mlir.constant(1 : i64)
// BODY-LABEL: llvm.func @consume.__obelisk_eval_body_0(
// BODY: llvm.call @obelisk_rt_v1_coverage_point_hit
// BODY: llvm.cond_br
// BODY-LABEL: llvm.func @consume.__obelisk_eval_body_0.__obelisk_two_state_0(
// BODY: llvm.call @obelisk_rt_v1_coverage_point_hit
// BODY: llvm.cond_br
// CLOCK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// CLOCK: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// CLOCK-COUNT-4: llvm.call @obelisk_rt_v1_coverage_point_hit

// The coroutine conversion must rewrite both checkpoint predicates as part
// of the generated closure, including dead dynamic-handle computations.
// The cold callback must publish its committed planes before returning to
// the runtime, which otherwise exports the pre-checkpoint state over them.
module attributes {
  obelisk.coverage.line_point_count = 4 : i64,
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @eval_dynamic_load {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_dynamic_load.root"
    simulation.code_unit.decl 2 in 0 always
        hierarchy "eval_dynamic_load.clock"
    simulation.code_unit.decl 3 in 0 always
        hierarchy "eval_dynamic_load.consume"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %data = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<32>>
      %index = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<64>>
      %sink = simulation.context.storage %ctx[3] :
          !simulation.ref<!simulation.logic<8>>
      %c = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %p = simulation.spawn @consume(%ctx, %clock, %data, %index, %sink) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<32>>,
          !simulation.ref<!simulation.logic<64>>,
          !simulation.ref<!simulation.logic<8>> -> !simulation.process
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
      %enabledWait = arith.constant true
      simulation.coverage.point_hit %ctx if %enabledWait[0] : !simulation.context
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %enabledToggle = arith.constant true
      simulation.coverage.point_hit %ctx if %enabledToggle[1] : !simulation.context
      %old = simulation.ref.load %clock :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @consume(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %data: !simulation.ref<!simulation.logic<32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %index: !simulation.ref<!simulation.logic<64>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64},
        %sink: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume
          {site = #schedule.continuation<id = 2>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %enabledBody = arith.constant true
      simulation.coverage.point_hit %ctx if %enabledBody[2] : !simulation.context
      %low = simulation.ref.load %index :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %slice = simulation.ref.dyn_extract %data from %low :
          (!simulation.ref<!simulation.logic<32>>,
           !simulation.logic<64>) -> !simulation.ref<!simulation.logic<8>>
      %value = simulation.ref.load %slice :
          !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.nba.enqueue %value to %sink :
          (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %bits = simulation.logic.to_bits %low : !simulation.logic<64> -> i64
      %zero = arith.constant 0 : i64
      %show = arith.cmpi ne, %bits, %zero : i64
      cf.cond_br %show, ^display, ^wait
    ^display:
      %enabledDisplay = arith.constant true
      simulation.coverage.point_hit %ctx if %enabledDisplay[3] : !simulation.context
      %format = simulation.bytes.constant "value=%h"
      %stdout = arith.constant 1 : i32
      %previous = simulation.ref.load %sink :
          !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %stdout(%format, %previous)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, !simulation.logic<8>
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: llvm.func @consume.__obelisk_eval_body_0.__obelisk_path_known
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @consume.__obelisk_eval_body_0.__obelisk_checkpoint_path
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.return
// CHECK: llvm.func @__obelisk_eval_four_state_fallback_v1_
// CHECK: llvm.call @__obelisk_eval_checkpoint_body_v1_
// CHECK: llvm.call @__obelisk_eval_dispatch_v1
// CHECK: llvm.call @obelisk_rt_v1_native_state_sync
