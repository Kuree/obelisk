// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// A path-sensitive promotion probe must not make a runtime checkpoint leaf
// part of the generated call closure. Cold leaves retain explicit Tier-3
// return routes rather than hidden runtime calls in a generated predicate.
//
// `guarded_blocking` reads back a blocking store on its fast path, which
// needs a private SSA overlay. Both predicates are read-only after promotion,
// and the model remains eligible for generated evaluation.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @path_promotion {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "path.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "path.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "path.guarded_nba"
    simulation.code_unit.decl 4 in 0 always hierarchy "path.guarded_blocking"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %source = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<1>>
      %nba_destination = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<1>>
      %blocking_destination = simulation.context.storage %ctx[3] :
          !simulation.ref<!simulation.logic<1>>
      %clock_process = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %nba_process = simulation.spawn @guarded_nba(
          %ctx, %clock, %source, %nba_destination) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %blocking_process = simulation.spawn @guarded_blocking(
          %ctx, %source, %source, %blocking_destination) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
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

    simulation.func @guarded_nba(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %source: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %destination: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume
          {site = #schedule.continuation<id = 2>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %value = simulation.ref.load %source :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %zero = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      %take_fast_path = simulation.logic.compare case_eq %value, %zero :
          (!simulation.logic<1>, !simulation.logic<1>) -> i1
      cf.cond_br %take_fast_path, ^publish, ^checkpoint
    ^publish:
      simulation.nba.enqueue %value to %destination :
          (!simulation.logic<1>,
           !simulation.ref<!simulation.logic<1>>) -> ()
      cf.br ^wait
    ^checkpoint:
      %stdout = arith.constant -2147483647 : i32
      %message = simulation.bytes.constant "nba checkpoint"
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      cf.br ^wait
    }

    simulation.func @guarded_blocking(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %source: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %destination: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume
          {site = #schedule.continuation<id = 3>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %value = simulation.ref.load %source :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %zero = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      %take_fast_path = simulation.logic.compare case_eq %value, %zero :
          (!simulation.logic<1>, !simulation.logic<1>) -> i1
      cf.cond_br %take_fast_path, ^publish, ^checkpoint
    ^publish:
      simulation.ref.store %value to %destination :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^read_back
    ^read_back:
      %stored = simulation.ref.load %destination :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %observe = simulation.logic.is_true %stored : !simulation.logic<1>
      cf.cond_br %observe, ^checkpoint, ^wait
    ^checkpoint:
      %stdout = arith.constant -2147483647 : i32
      %message = simulation.bytes.constant "blocking checkpoint"
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      cf.br ^wait
    }
  }
}

// CHECK: module attributes {{.*}}schedule.eval.generated
// CHECK-LABEL: llvm.func @guarded_blocking.__obelisk_eval_body_0.__obelisk_path_known
// CHECK-NOT: llvm.store
// CHECK-NOT: llvm.call
// CHECK: {{^  \}$}}
// CHECK-LABEL: llvm.func @guarded_blocking.__obelisk_eval_body_0.__obelisk_checkpoint_path
// CHECK-NOT: llvm.store
// CHECK-NOT: llvm.call
// CHECK: {{^  \}$}}
