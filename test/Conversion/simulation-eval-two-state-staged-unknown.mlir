// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --implicit-check-not=__obelisk_path_known

// IEEE 1800-2023 10.4.2, 4.4.2.4: an NBA updates its target with exactly the
// computed value. `join`'s two-state copy is entered with every input known,
// yet its `?:` payload reaches the enqueue through a block argument and can be
// X. The two-state NBA commits zero staged unknown data, so the staging site
// tests the unknown plane it computed and, only when it is nonzero, selects
// the step's four-state commit and marks the root `s` (bit 1). The known
// `n + 1` site stages two-state data without a test. `literal` enqueues an
// `'x` literal directly: the same staging test covers it, so its owner needs
// no per-activation path-known predicate.

// CHECK-LABEL: llvm.func @join.__obelisk_eval_body_0.__obelisk_two_state_0(
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// CHECK-NOT: __obelisk_eval_step_four_state
// CHECK: ^[[JOIN:bb[0-9]+]](%{{.*}}: i6, %[[UNKNOWN:.*]]: i6):
// CHECK: %[[WIDE:.*]] = llvm.zext %[[UNKNOWN]] : i6 to i64
// CHECK: %[[STAGED:.*]] = llvm.icmp "ne" %[[WIDE]], %{{.*}} : i64
// CHECK-NEXT: llvm.cond_br %[[STAGED]], ^[[MARK:bb[0-9]+]], ^[[STAGE:bb[0-9]+]]
// CHECK-NEXT: ^[[MARK]]:
// The store's independent operands may be emitted in either order.
// CHECK-DAG: %[[ONE:.*]] = llvm.mlir.constant(1 : i8)
// CHECK-DAG: %[[FALLBACK:.*]] = llvm.mlir.addressof @__obelisk_eval_step_four_state_fallback_v1
// CHECK-NEXT: llvm.store %[[ONE]], %[[FALLBACK]]
// CHECK-NEXT: %[[ROOTS:.*]] = llvm.mlir.addressof @__obelisk_eval_step_four_state_nba_roots_v1
// CHECK-NEXT: %[[WORD:.*]] = llvm.getelementptr %[[ROOTS]][0]
// CHECK-NEXT: %[[OLD:.*]] = llvm.load %[[WORD]]
// CHECK-NEXT: %[[BIT:.*]] = llvm.mlir.constant(2 : i64)
// CHECK-NEXT: %[[NEW:.*]] = llvm.or %[[OLD]], %[[BIT]]
// CHECK-NEXT: llvm.store %[[NEW]], %[[WORD]]
// CHECK-NEXT: llvm.br ^[[STAGE]]
// CHECK-NEXT: ^[[STAGE]]:
// CHECK-NOT: __obelisk_eval_step_four_state
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_1
// CHECK-NOT: __obelisk_eval_step_four_state
// CHECK: {{^  }}}
// CHECK-LABEL: llvm.func @literal.__obelisk_eval_body_0.__obelisk_two_state_0(
// CHECK: llvm.mlir.addressof @__obelisk_eval_step_four_state_fallback_v1
// CHECK: llvm.mlir.addressof @__obelisk_eval_step_four_state_nba_roots_v1

!clk = !simulation.ref<i1>
!l6 = !simulation.logic<6>
!r6 = !simulation.ref<!simulation.logic<6>>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.closed_executable} {
  simulation.design @staged_unknown {
    simulation.scope.decl 0 hierarchy "t"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "t.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "t.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "t.join"
    simulation.code_unit.decl 4 in 0 always hierarchy "t.reader"
    simulation.code_unit.decl 5 in 0 always hierarchy "t.literal"
    simulation.code_unit.decl 6 in 0 always hierarchy "t.literal_reader"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !l6 design
    simulation.storage.decl 2 in 0 : !l6 design
    simulation.storage.decl 3 in 0 : !l6 design
    simulation.storage.decl 4 in 0 : !l6 design
    simulation.storage.decl 5 in 0 : !l6 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clk
      %n = simulation.context.storage %ctx[1] : !r6
      %s = simulation.context.storage %ctx[2] : !r6
      %false = arith.constant false
      %zero = simulation.logic.constant 0 : i6, 0 : i6 : !l6
      simulation.ref.store %false to %clk : i1, !clk
      simulation.ref.store %zero to %n : !l6, !r6
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clk -> !simulation.process
      %j = simulation.spawn @join(%ctx, %clk, %n, %s) : !simulation.context, !clk, !r6, !r6 -> !simulation.process
      %y = simulation.context.storage %ctx[3] : !r6
      %r = simulation.spawn @reader(%ctx, %s, %y) : !simulation.context, !r6, !r6 -> !simulation.process
      %t = simulation.context.storage %ctx[4] : !r6
      %u = simulation.context.storage %ctx[5] : !r6
      %l = simulation.spawn @literal(%ctx, %clk, %n, %t) : !simulation.context, !clk, !r6, !r6 -> !simulation.process
      %lr = simulation.spawn @literal_reader(%ctx, %t, %u) : !simulation.context, !r6, !r6 -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !clk -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      simulation.ref.store %new to %clk : i1, !clk
      cf.br ^wait
    }
    // always @(posedge clk) begin n <= n + 1; s <= n[0] ? 'x : n; end
    simulation.func @join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %n: !r6 {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %s: !r6 {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 2>} : !clk
    ^body:
      %old = simulation.ref.load %n : !r6 -> !l6
      %one = simulation.logic.constant 1 : i6, 0 : i6 : !l6
      %next = simulation.logic.binary add %old, %one : !l6
      simulation.nba.enqueue %next to %n : (!l6, !r6) -> ()
      %odd = simulation.logic.extract %old from 0 : !l6 -> !simulation.logic<1>
      %high = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      %take = simulation.logic.compare case_eq %odd, %high : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      %unknown = simulation.logic.constant 0 : i6, -1 : i6 : !l6
      cf.cond_br %take, ^stage(%unknown : !l6), ^stage(%old : !l6)
    ^stage(%payload: !l6):
      simulation.nba.enqueue %payload to %s : (!l6, !r6) -> ()
      cf.br ^wait
    }
    // always @(s) y = s;
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %s: !r6 {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %y: !r6 {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %s to ^body {site = #schedule.continuation<id = 4>} : !r6
    ^body:
      %value = simulation.ref.load %s : !r6 -> !l6
      simulation.ref.store %value to %y : !l6, !r6
      cf.br ^wait
    }
    // always @(posedge clk) if (n[0]) t <= 'x; else t <= n;
    simulation.func @literal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %n: !r6 {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %t: !r6 {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 5>} : !clk
    ^body:
      %old = simulation.ref.load %n : !r6 -> !l6
      %odd = simulation.logic.extract %old from 0 : !l6 -> !simulation.logic<1>
      %high = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      %take = simulation.logic.compare case_eq %odd, %high : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      cf.cond_br %take, ^unknown, ^known
    ^unknown:
      %x = simulation.logic.constant 0 : i6, -1 : i6 : !l6
      simulation.nba.enqueue %x to %t : (!l6, !r6) -> ()
      cf.br ^wait
    ^known:
      simulation.nba.enqueue %old to %t : (!l6, !r6) -> ()
      cf.br ^wait
    }
    // always @(t) u = t;
    simulation.func @literal_reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %t: !r6 {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %u: !r6 {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %t to ^body {site = #schedule.continuation<id = 6>} : !r6
    ^body:
      %value = simulation.ref.load %t : !r6 -> !l6
      simulation.ref.store %value to %u : !l6, !r6
      cf.br ^wait
    }
  }
}
