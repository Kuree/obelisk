// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-suspension),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},convert-talos-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace

// The value proof retains only the two nibbles actually used from a whole
// packed read. The later high-half writer must not create a false backedge.
// TRACE: talos ranked activation: step2 valid=1 complete=1
// TRACE: talos eval boundary: owner=0 island=0 body=step1.__talos_eval_body_0 successors=1{{$}}
// TRACE-NEXT: talos eval boundary: owner=1 island=0 body=step2.__talos_eval_body_0 successors=2{{$}}
// TRACE-NEXT: talos eval boundary: owner=2 island=0 body=step3.__talos_eval_body_0 successors=3{{$}}
// TRACE-NEXT: talos eval boundary: owner=3 island=0 body=step4.__talos_eval_body_0 successors=4{{$}}
// TRACE-NEXT: talos eval dependency: owner=4 island=0 body=step5.__talos_eval_body_0 successors={{$}}

// Bounded reads use only the low half of storage 1. The final consumer writes
// its high half, so the coarse graph has a false cycle through net boundaries.
// The activation proof recovers the DAG; dispatcher ready bits must follow it.
!ref = !simulation.ref<i1>
!vref = !simulation.ref<!simulation.logic<8>>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.debug_timing} {
  simulation.design @ranked_ready_order {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !simulation.logic<16> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.net.decl 1 in 0 : !simulation.logic<8> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 8 reversed = false
    simulation.net.decl 2 in 0 : !simulation.logic<8> design
    // Keep the producer as a strength-aware boundary (LRM 6.3.2, 28.11),
    // even when plain single-driver wires are admitted to ranked groups.
    simulation.driver.decl 1 in 0 drives 2 : !simulation.logic<8> design
      {strength0 = 3 : i32, strength1 = 3 : i32}
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.storage.decl 4 in 0 : !simulation.logic<8> design
    simulation.storage.decl 5 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "step1"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "step2"
    simulation.code_unit.decl 5 in 0 always hierarchy "step3"
    simulation.code_unit.decl 6 in 0 always hierarchy "step4"
    simulation.code_unit.decl 7 in 0 always hierarchy "step5"
    simulation.code_unit.decl 8 in 0 initial hierarchy "check"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %shared = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<16>>
      %s2 = simulation.context.storage %ctx[2] : !vref
      %s3 = simulation.context.net %ctx[1] : !simulation.net<!simulation.logic<8>>
      %mirror = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %s4 = simulation.context.storage %ctx[4] : !vref
      %s5 = simulation.context.storage %ctx[5] : !vref
      %zero = arith.constant false
      simulation.ref.store %zero to %clk : i1, !ref
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p5 = simulation.spawn @step5(%ctx, %s4, %s5, %shared) : !simulation.context, !vref, !vref, !simulation.ref<!simulation.logic<16>> -> !simulation.process
      %p4 = simulation.spawn @step4(%ctx, %s3, %s4) : !simulation.context, !simulation.net<!simulation.logic<8>>, !vref -> !simulation.process
      %p3 = simulation.spawn @step3(%ctx, %s2, %driver) : !simulation.context, !vref, !simulation.driver<!simulation.logic<8>> -> !simulation.process
      %p2 = simulation.spawn @step2(%ctx, %shared, %s2) : !simulation.context, !simulation.ref<!simulation.logic<16>>, !vref -> !simulation.process
      %p1 = simulation.spawn @step1(%ctx, %clk, %shared, %mirror) : !simulation.context, !ref, !simulation.ref<!simulation.logic<16>>, !simulation.driver<!simulation.logic<8>> -> !simulation.process
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

    simulation.func @step1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %output: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %mirror: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 2>} : !ref
    ^body:
      %v = simulation.ref.load %input : !ref -> i1
      %bits = arith.extui %v : i1 to i8
      %wide = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %next = simulation.logic.binary add %wide, %one : !simulation.logic<8>
      %lane = simulation.ref.extract %output from 0 : !simulation.ref<!simulation.logic<16>> -> !vref
      simulation.ref.store %next to %lane : !simulation.logic<8>, !vref
      simulation.driver.drive %mirror = %next : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      cf.br ^wait
    }
    simulation.func @step2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      %four = arith.constant 4 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %two : i32
      cf.cond_br %more, ^copy, ^wait
    ^copy:
      %offset = arith.muli %i, %four : i32
      %in = simulation.ref.dyn_extract %input from %offset : (!simulation.ref<!simulation.logic<16>>, i32) -> !simulation.ref<!simulation.logic<4>>
      %out = simulation.ref.dyn_extract %output from %offset : (!vref, i32) -> !simulation.ref<!simulation.logic<4>>
      %whole = simulation.ref.load %input : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %value = simulation.logic.dyn_extract %whole from %offset : (!simulation.logic<16>, i32) -> !simulation.logic<4>
      %scratch = simulation.ref.alloc %value : !simulation.logic<4> -> !simulation.ref<!simulation.logic<4>>
      %choose = arith.cmpi eq, %i, %zero : i32
      %selected = arith.select %choose, %scratch, %in : !simulation.ref<!simulation.logic<4>>
      %copied = simulation.ref.load %selected : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %copied to %out : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^wait:
      %watch = simulation.ref.extract %input from 0 : !simulation.ref<!simulation.logic<16>> -> !vref
      simulation.suspend.change %watch to ^body {site = #schedule.continuation<id = 3>} : !vref
    }
    simulation.func @step3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %output: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 4>} : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %next = simulation.logic.binary add %v, %one : !simulation.logic<8>
      simulation.driver.drive %output = %next : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      cf.br ^wait
    }
    simulation.func @step4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.net<!simulation.logic<8>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 5>} : !simulation.net<!simulation.logic<8>>
    ^body:
      %v = simulation.net.read %input : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %next = simulation.logic.binary add %v, %one : !simulation.logic<8>
      simulation.ref.store %next to %output : !simulation.logic<8>, !vref
      cf.br ^wait
    }
    simulation.func @step5(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}, %feedback: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 6>} : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %next = simulation.logic.binary add %v, %one : !simulation.logic<8>
      simulation.ref.store %next to %output : !simulation.logic<8>, !vref
      %high = simulation.ref.extract %feedback from 8 : !simulation.ref<!simulation.logic<16>> -> !vref
      simulation.ref.store %next to %high : !simulation.logic<8>, !vref
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %output = simulation.context.storage %ctx[5] : !vref
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^first
    ^first:
      %a = simulation.ref.load %output : !vref -> !simulation.logic<8>
      %fmt = simulation.bytes.constant "chain %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %a) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      %again = simulation.time.constant 2
      simulation.suspend.delay %again to ^done
    ^done:
      %b = simulation.ref.load %output : !vref -> !simulation.logic<8>
      %fmt2 = simulation.bytes.constant "chain %0d"
      %channel2 = arith.constant 1 : i32
      simulation.display %ctx to %channel2(%fmt2, %b) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
