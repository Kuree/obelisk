// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off prune-native=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s --check-prefix=GROUP < %t.mlir

// The read-after-write temporary joins the ranked sweep. A read-before-write
// value and a conditional definition retain the original executor boundary.
// Runtime behavior is compared with bytecode at both O0/O3.
// TRACE-DAG: obelisk ranked activation: captured_state valid=1 complete=1
// TRACE-DAG: obelisk ranked activation: partial_definition valid=1 complete=0
// TRACE-DAG: obelisk ranked activation: temporary valid=1 complete=1
// TRACE: obelisk ranked groups: candidates=1 groups=1 segments=0 members=1 clocked_members=0
// TRACE: obelisk eval dependency: owner=2 island={{[0-9]+}} body=temporary.__obelisk_eval_body_0 successors=
// GROUP-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// GROUP-SAME: schedule.eval.ranked_members = array<i32: 2>

// Source-ordered temporaries and true activation state must have distinct
// activation-boundary summaries.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, obelisk.debug.native_timing} {
  simulation.design @activation_inputs {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "temporary"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "captured_state"
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "partial_definition"
    simulation.code_unit.decl 7 in 0 initial hierarchy "check"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i8>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      %c = simulation.spawn @partial_definition(%ctx, %r0, %r5, %r6) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %clock = simulation.spawn @clock(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @captured_state(%ctx, %r0, %r3, %r4) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %a = simulation.spawn @temporary(%ctx, %r0, %r1, %r2) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %check = simulation.spawn @check(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %next = arith.xori %old, %one : i8
      simulation.ref.store %next to %clock : i8, !simulation.ref<i8>
      cf.br ^wait
    }
    simulation.func @temporary(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %temp: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %temp : i8, !simulation.ref<i8>
      %read = simulation.ref.load %temp : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %read, %one : i8
      simulation.ref.store %next to %output : i8, !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^resume : !simulation.ref<i8>
    ^resume:
      cf.br ^body
    }
    simulation.func @captured_state(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %temp: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %read = simulation.ref.load %temp : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %temp : i8, !simulation.ref<i8>
      %one = arith.constant 1 : i8
      %next = arith.addi %read, %one : i8
      simulation.ref.store %next to %output : i8, !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^resume : !simulation.ref<i8>
    ^resume:
      cf.br ^body
    }
    simulation.func @partial_definition(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %temp: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i8> -> i8
      %zero = arith.constant 0 : i8
      %enabled = arith.cmpi ne, %value, %zero : i8
      cf.cond_br %enabled, ^publish, ^read
    ^publish:
      simulation.ref.store %value to %temp : i8, !simulation.ref<i8>
      cf.br ^read
    ^read:
      %read = simulation.ref.load %temp : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %read, %one : i8
      simulation.ref.store %next to %output : i8, !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^resume : !simulation.ref<i8>
    ^resume:
      cf.br ^body
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %a = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %b = simulation.context.storage %ctx[4] : !simulation.ref<i8>
      %c = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      %initial = simulation.time.constant 3
      simulation.suspend.delay %initial to ^first
    ^first:
      %av = simulation.ref.load %a : !simulation.ref<i8> -> i8
      %bv = simulation.ref.load %b : !simulation.ref<i8> -> i8
      %cv = simulation.ref.load %c : !simulation.ref<i8> -> i8
      %fmt = simulation.bytes.constant "values %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %av, %bv, %cv) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i8, i8, i8
      %again = simulation.time.constant 2
      simulation.suspend.delay %again to ^second
    ^second:
      %av2 = simulation.ref.load %a : !simulation.ref<i8> -> i8
      %bv2 = simulation.ref.load %b : !simulation.ref<i8> -> i8
      %cv2 = simulation.ref.load %c : !simulation.ref<i8> -> i8
      %fmt2 = simulation.bytes.constant "values %0d %0d %0d"
      %channel2 = arith.constant 1 : i32
      simulation.display %ctx to %channel2(%fmt2, %av2, %bv2, %cv2) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i8, i8, i8
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
