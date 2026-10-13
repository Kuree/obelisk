// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// A checkpoint predicate tracks exact-cell blocking writes across a CFG join.
// Both arms leave `count + 1` in the cell, so the predicate compares the
// forwarded value after the join without reloading state. The terminal
// partial write is not observed by any later probe read and stays out of the
// predicate. Neither predicate may publish a speculative update.

// CHECK-LABEL: llvm.func @work.__obelisk_eval_body_0.__obelisk_path_known_0(
// CHECK-DAG: %[[LIMIT:.*]] = llvm.mlir.constant(500 : i32)
// CHECK-NOT: llvm.{{store|call}}
// CHECK: %[[OLD:.*]] = llvm.load
// CHECK-NOT: llvm.{{load|store|call}}
// CHECK: %[[NEXT:.*]] = llvm.add %[[OLD]],
// CHECK-NOT: llvm.{{load|store|call}}
// CHECK: llvm.cond_br
// CHECK-NOT: llvm.{{load|store|call}}
// CHECK: llvm.icmp "eq" %[[NEXT]], %[[LIMIT]] : i32
// CHECK-NOT: llvm.{{load|store|call}}
// CHECK: {{^  }}}
// CHECK-LABEL: llvm.func @work.__obelisk_eval_body_0.__obelisk_checkpoint_path_0(
// CHECK-DAG: %[[LIMIT:.*]] = llvm.mlir.constant(500 : i32)
// CHECK-NOT: llvm.{{store|call}}
// CHECK: %[[OLD:.*]] = llvm.load
// CHECK-NOT: llvm.{{load|store|call}}
// CHECK: %[[NEXT:.*]] = llvm.add %[[OLD]],
// CHECK-NOT: llvm.{{load|store|call}}
// CHECK: llvm.cond_br
// CHECK-NOT: llvm.{{load|store|call}}
// CHECK: llvm.icmp "eq" %[[NEXT]], %[[LIMIT]] : i32
// CHECK-NOT: llvm.{{load|store|call}}
// CHECK: {{^  }}}

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
      %odd = arith.trunci %old : i32 to i1
      cf.cond_br %odd, ^observe, ^adjust
    ^adjust:
      %bumped = arith.addi %next, %one : i32
      simulation.ref.store %bumped to %count : i32, !simulation.ref<i32>
      %temporary = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %restored = arith.subi %temporary, %one : i32
      simulation.ref.store %restored to %count : i32, !simulation.ref<i32>
      cf.br ^observe
    ^observe:
      %observed = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %limit = arith.constant 500 : i32
      %done = arith.cmpi eq, %observed, %limit : i32
      // This terminal partial write overlaps the promoted count cell, but no
      // later probe read can observe it. Keep it in the real activation only.
      %upper = simulation.ref.extract %count from 16 : !simulation.ref<i32> -> !simulation.ref<i16>
      %zero16 = arith.constant 0 : i16
      simulation.ref.store %zero16 to %upper : i16, !simulation.ref<i16>
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
