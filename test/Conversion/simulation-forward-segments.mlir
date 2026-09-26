// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN --implicit-check-not='@__obelisk_eval_fast_coordinator' --implicit-check-not='@__obelisk_eval_steady_two_state_coordinator' --implicit-check-not='@__obelisk_eval_periodic_two_state_coordinator' --implicit-check-not='@__obelisk_eval_periodic_promotion' --implicit-check-not='obelisk.eval.forward_segment_edges' < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=GROUP < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=STATE < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-forward-segments.test.

// Reversed source spawn order still produces dependency-ranked computation.
// The old model-wide forward-scan controller is absent.
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: obelisk.eval.ranked_members = array<i32: 0, 1, 2, 3, 4>
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0
// GROUP-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// GROUP-SAME: obelisk.eval.dataflow_fallback = @__obelisk_eval_ranked_group_0.fallback
// GROUP-SAME: obelisk.eval.materialized_group_calls = 5 : i64
// GROUP-SAME: obelisk.eval.predicated_dataflow
// GROUP-NOT: llvm.switch
// GROUP-NOT: llvm.call %
// GROUP: llvm.return
// GROUP-LABEL: llvm.func @__obelisk_eval_ranked_group_0.fallback(
// GROUP-SAME: obelisk.eval.ssa_ready_words = 1 : i64
// STATE-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// STATE-SAME: obelisk.eval.predicated_dataflow
// STATE: llvm.add
// STATE: llvm.add
// STATE: llvm.add
// STATE: llvm.add
// STATE: llvm.add
// STATE: llvm.return
// STATE-LABEL: llvm.func @__obelisk_eval_ranked_group_0.fallback(
// STATE-SAME: obelisk.eval.ssa_value_ranges = 4 : i64
!ref = !obelisk_sim.ref<i1>
!vref = !obelisk_sim.ref<i8>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @forward_chain {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i8 design
    obelisk_sim.storage.decl 2 in 0 : i8 design
    obelisk_sim.storage.decl 3 in 0 : i8 design
    obelisk_sim.storage.decl 4 in 0 : i8 design
    obelisk_sim.storage.decl 5 in 0 : i8 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "step1"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "step2"
    obelisk_sim.code_unit.decl 5 in 0 always hierarchy "step3"
    obelisk_sim.code_unit.decl 6 in 0 always hierarchy "step4"
    obelisk_sim.code_unit.decl 7 in 0 always hierarchy "step5"
    obelisk_sim.code_unit.decl 8 in 0 initial hierarchy "check"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !ref
      %s1 = obelisk_sim.context.storage %ctx[1] : !vref
      %s2 = obelisk_sim.context.storage %ctx[2] : !vref
      %s3 = obelisk_sim.context.storage %ctx[3] : !vref
      %s4 = obelisk_sim.context.storage %ctx[4] : !vref
      %s5 = obelisk_sim.context.storage %ctx[5] : !vref
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clk : i1, !ref
      %clock = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !ref -> !obelisk_sim.process
      %p5 = obelisk_sim.spawn @step5(%ctx, %s4, %s5) : !obelisk_sim.context, !vref, !vref -> !obelisk_sim.process
      %p4 = obelisk_sim.spawn @step4(%ctx, %s3, %s4) : !obelisk_sim.context, !vref, !vref -> !obelisk_sim.process
      %p3 = obelisk_sim.spawn @step3(%ctx, %s2, %s3) : !obelisk_sim.context, !vref, !vref -> !obelisk_sim.process
      %p2 = obelisk_sim.spawn @step2(%ctx, %s1, %s2) : !obelisk_sim.context, !vref, !vref -> !obelisk_sim.process
      %p1 = obelisk_sim.spawn @step1(%ctx, %clk, %s1) : !obelisk_sim.context, !ref, !vref -> !obelisk_sim.process
      %c = obelisk_sim.spawn @check(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^toggle {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !ref -> i1
      %one = arith.constant true
      %new = arith.xori %old, %one : i1
      obelisk_sim.ref.store %new to %clk : i1, !ref
      cf.br ^wait
    }

    obelisk_sim.func @step1(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %output: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %input to ^body {site = #obelisk_sim.continuation<id = 2>} : !ref
    ^body:
      %v = obelisk_sim.ref.load %input : !ref -> i1
      %wide = arith.extui %v : i1 to i8
      %one = arith.constant 1 : i8
      %next = arith.addi %wide, %one : i8
      obelisk_sim.ref.store %next to %output : i8, !vref
      cf.br ^wait
    }
    obelisk_sim.func @step2(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %output: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %input to ^body {site = #obelisk_sim.continuation<id = 3>} : !vref
    ^body:
      %v = obelisk_sim.ref.load %input : !vref -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %v, %one : i8
      obelisk_sim.ref.store %next to %output : i8, !vref
      cf.br ^wait
    }
    obelisk_sim.func @step3(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %output: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %input to ^body {site = #obelisk_sim.continuation<id = 4>} : !vref
    ^body:
      %v = obelisk_sim.ref.load %input : !vref -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %v, %one : i8
      obelisk_sim.ref.store %next to %output : i8, !vref
      cf.br ^wait
    }
    obelisk_sim.func @step4(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %output: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %input to ^body {site = #obelisk_sim.continuation<id = 5>} : !vref
    ^body:
      %v = obelisk_sim.ref.load %input : !vref -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %v, %one : i8
      obelisk_sim.ref.store %next to %output : i8, !vref
      cf.br ^wait
    }
    obelisk_sim.func @step5(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}, %output: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 5 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %input to ^body {site = #obelisk_sim.continuation<id = 6>} : !vref
    ^body:
      %v = obelisk_sim.ref.load %input : !vref -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %v, %one : i8
      obelisk_sim.ref.store %next to %output : i8, !vref
      cf.br ^wait
    }
    obelisk_sim.func @check(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %output = obelisk_sim.context.storage %ctx[5] : !vref
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay to ^first
    ^first:
      %a = obelisk_sim.ref.load %output : !vref -> i8
      %fmt = obelisk_sim.bytes.constant "chain %0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %a) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, i8
      %again = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %again to ^done
    ^done:
      %b = obelisk_sim.ref.load %output : !vref -> i8
      %fmt2 = obelisk_sim.bytes.constant "chain %0d"
      %channel2 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel2(%fmt2, %b) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, i8
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }
  }
}

// Only startup and the timed checker use node dispatch; clock work must
// execute through the native coordinator at both optimization levels.
