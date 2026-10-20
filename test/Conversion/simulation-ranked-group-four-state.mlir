// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: schedule.eval.ranked_members = array<i32: 0, 1, 2, 3, 4>
// Members compare both planes before waking a consumer, so the transition to
// X and the later recovery from X both publish.
// PLAN: llvm.load {{.*}}schedule.eval.group_owner = 0 : i32} : !llvm.ptr -> !llvm.ptr
// PLAN-NEXT: %[[OLD_VALUE:.*]] = llvm.zext %{{.*}} : i8 to i64
// PLAN-NEXT: %[[OLD_UNKNOWN:.*]] = llvm.zext %{{.*}} : i8 to i64
// PLAN-NEXT: %[[NEW_VALUE:.*]] = llvm.zext %{{.*}} : i8 to i64
// PLAN-NEXT: %[[NEW_UNKNOWN:.*]] = llvm.zext %{{.*}} : i8 to i64
// PLAN-DAG: %[[VALUE_DELTA:.*]] = llvm.xor %[[OLD_VALUE]], %[[NEW_VALUE]] : i64
// PLAN-DAG: %[[UNKNOWN_DELTA:.*]] = llvm.xor %[[OLD_UNKNOWN]], %[[NEW_UNKNOWN]] : i64
// PLAN-NEXT: %[[DELTA:.*]] = llvm.or %[[VALUE_DELTA]], %[[UNKNOWN_DELTA]] : i64
// PLAN: llvm.icmp "eq" %[[DELTA]], %{{.*}} : i64
// PLAN-NOT: llvm.call %
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0

!ref = !simulation.ref<i1>
!word = !simulation.logic<8>
!vref = !simulation.ref<!word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.debug_timing} {
  simulation.design @forward_chain {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.storage.decl 3 in 0 : !word design
    simulation.storage.decl 4 in 0 : !word design
    simulation.storage.decl 5 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "step1"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "step2"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "step3"
    simulation.code_unit.decl 6 in 0 continuous hierarchy "step4"
    simulation.code_unit.decl 7 in 0 continuous hierarchy "step5"
    simulation.code_unit.decl 8 in 0 initial hierarchy "check"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %s1 = simulation.context.storage %ctx[1] : !vref
      %s2 = simulation.context.storage %ctx[2] : !vref
      %s3 = simulation.context.storage %ctx[3] : !vref
      %s4 = simulation.context.storage %ctx[4] : !vref
      %s5 = simulation.context.storage %ctx[5] : !vref
      %zero = arith.constant false
      simulation.ref.store %zero to %clk : i1, !ref
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p5 = simulation.spawn @step5(%ctx, %s4, %s5) : !simulation.context, !vref, !vref -> !simulation.process
      %p4 = simulation.spawn @step4(%ctx, %s3, %s4) : !simulation.context, !vref, !vref -> !simulation.process
      %p3 = simulation.spawn @step3(%ctx, %s2, %s3) : !simulation.context, !vref, !vref -> !simulation.process
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
      %v = simulation.ref.load %input : !ref -> i1
      %high = simulation.logic.constant 2 : i8, 0 : i8 : !word
      %low = simulation.logic.constant 255 : i8, 255 : i8 : !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = arith.select %v, %high, %low : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @step2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 3>} : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @step3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 4>} : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @step4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 6 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 5>} : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @step5(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 7 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 6>} : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %output = simulation.context.storage %ctx[5] : !vref
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^first
    ^first:
      %a = simulation.ref.load %output : !vref -> !word
      %fmt = simulation.bytes.constant "chain %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %a) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %again = simulation.time.constant 2
      simulation.suspend.delay %again to ^done
    ^done:
      %b = simulation.ref.load %output : !vref -> !word
      %fmt2 = simulation.bytes.constant "chain %0d"
      %channel2 = arith.constant 1 : i32
      simulation.display %ctx to %channel2(%fmt2, %b) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %thirdDelay = simulation.time.constant 2
      simulation.suspend.delay %thirdDelay to ^third
    ^third:
      %c = simulation.ref.load %output : !vref -> !word
      %fmt3 = simulation.bytes.constant "chain %0d"
      %channel3 = arith.constant 1 : i32
      simulation.display %ctx to %channel3(%fmt3, %c) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
