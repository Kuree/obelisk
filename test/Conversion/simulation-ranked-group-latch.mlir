// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-ranked-group-latch.test.

// IEEE 1800-2023 9.2.2.3: a closed latch holds its last assigned
// bits, including untouched X bits. Data changes while closed; reopening
// captures the new data. The latch shares a ranked helper with its neighbors;
// its keyword alone must not force an executor downgrade.
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: schedule.eval.ranked_members = array<i32: 0, 1, 2, 3>
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0
!ref = !simulation.ref<i1>
!vref = !simulation.ref<i8>
!word = !simulation.logic<8>
!lref = !simulation.ref<!word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  simulation.design @forward_chain {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : !word design
    simulation.storage.decl 4 in 0 : !word design
    simulation.storage.decl 5 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "step1"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "step2"
    simulation.code_unit.decl 5 in 0 always_latch hierarchy "step3"
    simulation.code_unit.decl 6 in 0 continuous hierarchy "step4"
    simulation.code_unit.decl 7 in 0 continuous hierarchy "step5"
    simulation.code_unit.decl 8 in 0 initial hierarchy "check"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %s1 = simulation.context.storage %ctx[1] : !vref
      %s2 = simulation.context.storage %ctx[2] : !vref
      %s3 = simulation.context.storage %ctx[3] : !lref
      %s4 = simulation.context.storage %ctx[4] : !lref
      %s5 = simulation.context.storage %ctx[5] : !lref
      %zero = arith.constant false
      simulation.ref.store %zero to %clk : i1, !ref
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p5 = simulation.spawn @step5(%ctx, %s4, %s5) : !simulation.context, !lref, !lref -> !simulation.process
      %p4 = simulation.spawn @step4(%ctx, %s3, %s4) : !simulation.context, !lref, !lref -> !simulation.process
      %p3 = simulation.spawn @step3(%ctx, %s2, %s3, %clk) : !simulation.context, !vref, !lref, !ref -> !simulation.process
      %p2 = simulation.spawn @step2(%ctx, %s1, %s2) : !simulation.context, !vref, !vref -> !simulation.process
      %p1 = simulation.spawn @step1(%ctx, %clk, %s1) : !simulation.context, !ref, !vref -> !simulation.process
      %c = simulation.spawn @check(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !ref -> i1
      %one = arith.constant true
      %new = arith.xori %old, %one : i1
      simulation.ref.store %new to %clk : i1, !ref
      cf.br ^wait
    }

    simulation.func @step1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 2>} : !ref
    ^body:
      %wide = simulation.ref.load %output : !vref -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %wide, %one : i8
      simulation.ref.store %next to %output : i8, !vref
      cf.br ^wait
    }
    simulation.func @step2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 3>} : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %v, %one : i8
      simulation.ref.store %next to %output : i8, !vref
      cf.br ^wait
    }
    simulation.func @step3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %output: !lref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %enable: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 6 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.any %input, %enable edges [0, 0] to ^body {site = #schedule.continuation<id = 4>} : !vref, !ref
    ^body:
      %open = simulation.ref.load %enable : !ref -> i1
      cf.cond_br %open, ^assign, ^wait
    ^assign:
      %value = simulation.ref.load %input : !vref -> i8
      %bits = arith.trunci %value : i8 to i4
      %data = simulation.logic.from_bits %bits : i4 -> !simulation.logic<4>
      %low = simulation.ref.extract %output from 0 : !lref -> !simulation.ref<!simulation.logic<4>>
      simulation.ref.store %data to %low : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      cf.br ^wait
    }
    simulation.func @step4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !lref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %output: !lref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 6 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 5>} : !lref
    ^body:
      %v = simulation.ref.load %input : !lref -> !word
      simulation.ref.store %v to %output : !word, !lref
      cf.br ^wait
    }
    simulation.func @step5(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !lref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %output: !lref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 7 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 6>} : !lref
    ^body:
      %v = simulation.ref.load %input : !lref -> !word
      simulation.ref.store %v to %output : !word, !lref
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %output = simulation.context.storage %ctx[5] : !lref
      %first = simulation.time.constant 1
      simulation.suspend.delay %first to ^closed
    ^closed:
      %a = simulation.ref.load %output : !lref -> !word
      %fmt0 = simulation.bytes.constant "latch %b"
      %channel0 = arith.constant 1 : i32
      simulation.display %ctx to %channel0(%fmt0, %a) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !word
      %next0 = simulation.time.constant 2
      simulation.suspend.delay %next0 to ^opened
    ^opened:
      %b = simulation.ref.load %output : !lref -> !word
      %fmt1 = simulation.bytes.constant "latch %b"
      %channel1 = arith.constant 1 : i32
      simulation.display %ctx to %channel1(%fmt1, %b) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !word
      %source = simulation.context.storage %ctx[1] : !vref
      %four = arith.constant 4 : i8
      simulation.ref.store %four to %source : i8, !vref
      %delta = simulation.time.constant 0
      simulation.suspend.delay %delta to ^transparent
    ^transparent:
      %changed = simulation.ref.load %output : !lref -> !word
      %fmtChanged = simulation.bytes.constant "latch %b"
      %channelChanged = arith.constant 1 : i32
      simulation.display %ctx to %channelChanged(%fmtChanged, %changed) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !word
      %next1 = simulation.time.constant 2
      simulation.suspend.delay %next1 to ^held
    ^held:
      %c = simulation.ref.load %output : !lref -> !word
      %fmt2 = simulation.bytes.constant "latch %b"
      %channel2 = arith.constant 1 : i32
      simulation.display %ctx to %channel2(%fmt2, %c) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !word
      %next2 = simulation.time.constant 2
      simulation.suspend.delay %next2 to ^reopened
    ^reopened:
      %d = simulation.ref.load %output : !lref -> !word
      %fmt3 = simulation.bytes.constant "latch %b"
      %channel3 = arith.constant 1 : i32
      simulation.display %ctx to %channel3(%fmt3, %d) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !word
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
