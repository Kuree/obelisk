// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// IEEE 1800-2023 20.2: $finish, $stop and $fatal end the current time step.
// An explicit termination inside an eval checkpoint body calls its own runtime
// entry point after staging the pending NBA, and returns the runtime status
// instead of continuing the body. The checkpoint driver polls the runtime
// after every body and stops dispatching once termination was requested, so
// no further activation or NBA commit runs in that time step.

// CHECK-LABEL: llvm.func @__obelisk_eval_checkpoint_body_v1_0(
// CHECK: llvm.store {{.*}}schedule.eval.pending_dirty_root
// CHECK: %[[STATUS:.*]] = llvm.call @obelisk_rt_v1_scheduler_fatal(%arg0,
// CHECK-NEXT: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
// CHECK-NEXT: %[[CONTINUE:.*]] = llvm.icmp "eq" %[[STATUS]], %[[ZERO]]
// CHECK-NEXT: llvm.cond_br %[[CONTINUE]]
// CHECK-LABEL: llvm.func @__obelisk_eval_four_state_fallback_v1_0(
// CHECK: %[[BODY:.*]] = llvm.call @__obelisk_eval_checkpoint_body_v1_0(%arg0)
// CHECK: %[[IDLE:.*]] = llvm.icmp "eq" %[[BODY]],
// CHECK: %[[REQUESTED:.*]] = llvm.call @obelisk_rt_v1_scheduler_termination_requested(%arg0)
// CHECK: %[[RUNNING:.*]] = llvm.icmp "eq" %[[REQUESTED]],
// CHECK: %[[NEXT:.*]] = llvm.and %[[IDLE]], %[[RUNNING]]
// CHECK: llvm.cond_br %[[NEXT]]

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 0 : i32
} {
  simulation.design @termination {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %count = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %false = arith.constant false
      simulation.ref.store %zero to %count : i32, !simulation.ref<i32>
      simulation.ref.store %false to %clock : i1, !simulation.ref<i1>
      %c = simulation.spawn @clock(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clock, %count) : !simulation.context, !simulation.ref<i1>, !simulation.ref<i32> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      simulation.ref.store %new to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %count: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume : !simulation.ref<i1>
    ^resume:
      %old = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %count : i32, !simulation.ref<i32>
      %observed = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %limit = arith.constant 500 : i32
      %done = arith.cmpi eq, %observed, %limit : i32
      cf.cond_br %done, ^terminate, ^wait
    ^terminate:
      %nba = arith.constant 999 : i32
      simulation.nba.enqueue %nba to %count : (i32, !simulation.ref<i32>) -> ()
      %verbosity = arith.constant 0 : i32
      simulation.fatal %ctx, %verbosity
      simulation.return
    }
  }
}

// -----

// CHECK-LABEL: llvm.func @__obelisk_eval_checkpoint_body_v1_0(
// CHECK: llvm.store {{.*}}schedule.eval.pending_dirty_root
// CHECK: %[[STATUS:.*]] = llvm.call @obelisk_rt_v1_scheduler_finish(%arg0,
// CHECK-NEXT: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
// CHECK-NEXT: %[[CONTINUE:.*]] = llvm.icmp "eq" %[[STATUS]], %[[ZERO]]
// CHECK-NEXT: llvm.cond_br %[[CONTINUE]]
// CHECK-LABEL: llvm.func @__obelisk_eval_four_state_fallback_v1_0(
// CHECK: %[[BODY:.*]] = llvm.call @__obelisk_eval_checkpoint_body_v1_0(%arg0)
// CHECK: %[[IDLE:.*]] = llvm.icmp "eq" %[[BODY]],
// CHECK: %[[REQUESTED:.*]] = llvm.call @obelisk_rt_v1_scheduler_termination_requested(%arg0)
// CHECK: %[[RUNNING:.*]] = llvm.icmp "eq" %[[REQUESTED]],
// CHECK: %[[NEXT:.*]] = llvm.and %[[IDLE]], %[[RUNNING]]
// CHECK: llvm.cond_br %[[NEXT]]

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 0 : i32
} {
  simulation.design @termination {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %count = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %false = arith.constant false
      simulation.ref.store %zero to %count : i32, !simulation.ref<i32>
      simulation.ref.store %false to %clock : i1, !simulation.ref<i1>
      %c = simulation.spawn @clock(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clock, %count) : !simulation.context, !simulation.ref<i1>, !simulation.ref<i32> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      simulation.ref.store %new to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %count: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume : !simulation.ref<i1>
    ^resume:
      %old = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %count : i32, !simulation.ref<i32>
      %observed = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %limit = arith.constant 500 : i32
      %done = arith.cmpi eq, %observed, %limit : i32
      cf.cond_br %done, ^terminate, ^wait
    ^terminate:
      %nba = arith.constant 999 : i32
      simulation.nba.enqueue %nba to %count : (i32, !simulation.ref<i32>) -> ()
      %verbosity = arith.constant 0 : i32
      simulation.finish %ctx, %verbosity
      simulation.return
    }
  }
}

// -----

// CHECK-LABEL: llvm.func @__obelisk_eval_checkpoint_body_v1_0(
// CHECK: llvm.store {{.*}}schedule.eval.pending_dirty_root
// CHECK: %[[STATUS:.*]] = llvm.call @obelisk_rt_v1_scheduler_stop(%arg0,
// CHECK-NEXT: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
// CHECK-NEXT: %[[CONTINUE:.*]] = llvm.icmp "eq" %[[STATUS]], %[[ZERO]]
// CHECK-NEXT: llvm.cond_br %[[CONTINUE]]
// CHECK-LABEL: llvm.func @__obelisk_eval_four_state_fallback_v1_0(
// CHECK: %[[BODY:.*]] = llvm.call @__obelisk_eval_checkpoint_body_v1_0(%arg0)
// CHECK: %[[IDLE:.*]] = llvm.icmp "eq" %[[BODY]],
// CHECK: %[[REQUESTED:.*]] = llvm.call @obelisk_rt_v1_scheduler_termination_requested(%arg0)
// CHECK: %[[RUNNING:.*]] = llvm.icmp "eq" %[[REQUESTED]],
// CHECK: %[[NEXT:.*]] = llvm.and %[[IDLE]], %[[RUNNING]]
// CHECK: llvm.cond_br %[[NEXT]]

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 0 : i32
} {
  simulation.design @termination {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %count = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %false = arith.constant false
      simulation.ref.store %zero to %count : i32, !simulation.ref<i32>
      simulation.ref.store %false to %clock : i1, !simulation.ref<i1>
      %c = simulation.spawn @clock(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clock, %count) : !simulation.context, !simulation.ref<i1>, !simulation.ref<i32> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      simulation.ref.store %new to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %count: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume : !simulation.ref<i1>
    ^resume:
      %old = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %count : i32, !simulation.ref<i32>
      %observed = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %limit = arith.constant 500 : i32
      %done = arith.cmpi eq, %observed, %limit : i32
      cf.cond_br %done, ^terminate, ^wait
    ^terminate:
      %nba = arith.constant 999 : i32
      simulation.nba.enqueue %nba to %count : (i32, !simulation.ref<i32>) -> ()
      %verbosity = arith.constant 0 : i32
      simulation.stop %ctx, %verbosity
      simulation.return
    }
  }
}
