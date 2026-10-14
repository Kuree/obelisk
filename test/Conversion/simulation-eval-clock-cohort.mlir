// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.serial 2> %t.serial.trace
// RUN: diff %t.mlir %t.serial
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s --check-prefix=GROUP --implicit-check-not=schedule.eval.predicated_dataflow < %t.mlir

// Exact physical edge identity determines cohorts. Each member still tests its
// own ready bit; opposite edges, other clocks, and async resets cannot cause
// speculative executions. A blocking temporary preserves source statement
// order and publication, while NBA stores stay behind the normal NBA barrier.
!clock = !simulation.ref<i1>
!word = !simulation.ref<i8>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, debug.native_timing} {
  simulation.design @cohorts {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock0"
    simulation.code_unit.decl 3 in 0 always hierarchy "clock1"
    simulation.storage.decl 2 in 0 : i8 design
    simulation.scope.decl 2 parent 0
    simulation.code_unit.decl 4 in 2 always hierarchy "rise_a"
    simulation.storage.decl 3 in 0 : i8 design
    simulation.scope.decl 3 parent 0
    simulation.code_unit.decl 5 in 3 always hierarchy "rise_b"
    simulation.storage.decl 4 in 0 : i8 design
    simulation.scope.decl 4 parent 0
    simulation.code_unit.decl 6 in 4 always hierarchy "fall_a"
    simulation.storage.decl 5 in 0 : i8 design
    simulation.scope.decl 5 parent 0
    simulation.code_unit.decl 7 in 5 always hierarchy "fall_b"
    simulation.storage.decl 6 in 0 : i8 design
    simulation.scope.decl 6 parent 0
    simulation.code_unit.decl 8 in 6 always hierarchy "other_a"
    simulation.storage.decl 7 in 0 : i8 design
    simulation.scope.decl 7 parent 0
    simulation.code_unit.decl 9 in 7 always hierarchy "other_b"
    simulation.storage.decl 8 in 0 : i8 design
    simulation.scope.decl 8 parent 0
    simulation.code_unit.decl 10 in 8 always hierarchy "async_reset"
    simulation.storage.decl 9 in 0 : i8 design
    simulation.scope.decl 9 parent 0
    simulation.code_unit.decl 11 in 9 always hierarchy "change"
    simulation.storage.decl 10 in 0 : i8 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clock
      %other = simulation.context.storage %ctx[1] : !clock
      %c0 = simulation.spawn @clock0(%ctx, %clk) : !simulation.context, !clock -> !simulation.process
      %c1 = simulation.spawn @clock1(%ctx, %other) : !simulation.context, !clock -> !simulation.process
      %rise_a = simulation.spawn @rise_a(%ctx, %clk, %other) : !simulation.context, !clock, !clock -> !simulation.process
      %rise_b = simulation.spawn @rise_b(%ctx, %clk, %other) : !simulation.context, !clock, !clock -> !simulation.process
      %fall_a = simulation.spawn @fall_a(%ctx, %clk, %other) : !simulation.context, !clock, !clock -> !simulation.process
      %fall_b = simulation.spawn @fall_b(%ctx, %clk, %other) : !simulation.context, !clock, !clock -> !simulation.process
      %other_a = simulation.spawn @other_a(%ctx, %clk, %other) : !simulation.context, !clock, !clock -> !simulation.process
      %other_b = simulation.spawn @other_b(%ctx, %clk, %other) : !simulation.context, !clock, !clock -> !simulation.process
      %async_reset = simulation.spawn @async_reset(%ctx, %clk, %other) : !simulation.context, !clock, !clock -> !simulation.process
      %change = simulation.spawn @change(%ctx, %clk, %other) : !simulation.context, !clock, !clock -> !simulation.process
      simulation.return
    }
    simulation.func @clock0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !clock -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clk : i1, !clock
      cf.br ^wait
    }
    simulation.func @clock1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !clock -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clk : i1, !clock
      cf.br ^wait
    }
    simulation.func @rise_a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %other: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !clock
    ^body:
      %out = simulation.context.storage %ctx[2] : !word
      %old = simulation.ref.load %out : !word -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      simulation.nba.enqueue %next to %out : (i8, !word) -> ()
      cf.br ^wait
    }
    simulation.func @rise_b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %other: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !clock
    ^body:
      %out = simulation.context.storage %ctx[3] : !word
      %old = simulation.ref.load %out : !word -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      %temp = simulation.context.storage %ctx[10] : !word
      simulation.ref.store %next to %temp : i8, !word
      %reload = simulation.ref.load %temp : !word -> i8
      simulation.nba.enqueue %reload to %out : (i8, !word) -> ()
      cf.br ^wait
    }
    simulation.func @fall_a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %other: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !clock
    ^body:
      %out = simulation.context.storage %ctx[4] : !word
      %old = simulation.ref.load %out : !word -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      simulation.nba.enqueue %next to %out : (i8, !word) -> ()
      cf.br ^wait
    }
    simulation.func @fall_b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %other: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !clock
    ^body:
      %out = simulation.context.storage %ctx[5] : !word
      %old = simulation.ref.load %out : !word -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      simulation.nba.enqueue %next to %out : (i8, !word) -> ()
      cf.br ^wait
    }
    simulation.func @other_a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %other: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 8 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %other to ^body : !clock
    ^body:
      %out = simulation.context.storage %ctx[6] : !word
      %old = simulation.ref.load %out : !word -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      simulation.nba.enqueue %next to %out : (i8, !word) -> ()
      cf.br ^wait
    }
    simulation.func @other_b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %other: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 9 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %other to ^body : !clock
    ^body:
      %out = simulation.context.storage %ctx[7] : !word
      %old = simulation.ref.load %out : !word -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      simulation.nba.enqueue %next to %out : (i8, !word) -> ()
      cf.br ^wait
    }
    simulation.func @async_reset(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %other: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 10 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.any %clk, %other edges [1, 1] to ^body : !clock, !clock
    ^body:
      %out = simulation.context.storage %ctx[8] : !word
      %old = simulation.ref.load %out : !word -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      simulation.nba.enqueue %next to %out : (i8, !word) -> ()
      cf.br ^wait
    }
    simulation.func @change(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %other: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 11 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^body : !clock
    ^body:
      %out = simulation.context.storage %ctx[9] : !word
      %old = simulation.ref.load %out : !word -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      simulation.nba.enqueue %next to %out : (i8, !word) -> ()
      cf.br ^wait
    }
  }
}

// TRACE: obelisk ranked groups: candidates=6 groups=3 segments=0 members=6 clocked_members=6
// Change-sensitive and multi-trigger bodies retain ordinary executors.
// TRACE: obelisk eval owner: bit=0 {{.*}} group=- executor=
// TRACE: obelisk eval owner: bit=1 {{.*}} group=- executor=
// TRACE: obelisk eval dependency: owner=2 island=0 body=fall_a.__obelisk_eval_body_0
// TRACE-NEXT: obelisk eval dependency: owner=3 island=0 body=fall_b.__obelisk_eval_body_0
// TRACE-NEXT: obelisk eval dependency: owner=4 island=1 body=other_a.__obelisk_eval_body_0
// TRACE-NEXT: obelisk eval dependency: owner=5 island=1 body=other_b.__obelisk_eval_body_0
// TRACE-NEXT: obelisk eval dependency: owner=6 island=2 body=rise_a.__obelisk_eval_body_0
// TRACE-NEXT: obelisk eval dependency: owner=7 island=2 body=rise_b.__obelisk_eval_body_0

// GROUP-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// GROUP-SAME: schedule.eval.ranked_members = array<i32: 2, 3>
// GROUP-LABEL: llvm.func @__obelisk_eval_ranked_group_1(
// GROUP-SAME: schedule.eval.ranked_members = array<i32: 4, 5>
// GROUP-LABEL: llvm.func @__obelisk_eval_ranked_group_2(
// GROUP-SAME: schedule.eval.ranked_members = array<i32: 6, 7>
// Both ready predicates precede their corresponding inline bodies.
// GROUP: llvm.mlir.constant(64 : i64)
// GROUP: llvm.and
// GROUP: llvm.icmp "ne"
// GROUP: llvm.cond_br
// GROUP: llvm.mlir.constant(128 : i64)
// GROUP: llvm.and
// GROUP: llvm.icmp "ne"
// GROUP: llvm.cond_br
// GROUP: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_
// GROUP: llvm.store {{.*}}schedule.eval.group_owner = 6 : i32
// GROUP: llvm.store {{.*}}schedule.eval.group_owner = 7 : i32
