// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | diff %t.mlir -
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=VPI --implicit-check-not=__obelisk_eval_group_promotion_

// VPI-LABEL: llvm.func @main(

// Eight continuous members retain the original ordered propagation. A display
// separates their group from the four-state producer; X/Z inputs must use the
// unchanged fallback and later known inputs can establish an independent proof.
// Runtime O0/O3 and actual required-bytecode execution are checked separately.
// CHECK-DAG: llvm.mlir.global internal @__obelisk_eval_group_promotion_latched_v1
// CHECK-LABEL: llvm.func @__obelisk_eval_promotion_invalidate_v1()
// CHECK: llvm.mlir.addressof @__obelisk_eval_kernel_promotion_latched_v1
// CHECK: "llvm.intr.memset"
// CHECK: llvm.mlir.addressof @__obelisk_eval_group_promotion_latched_v1
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK: "llvm.intr.memset"
// CHECK: llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// CHECK: llvm.mlir.constant(510 : i64)
// CHECK-LABEL: llvm.func @__obelisk_eval_group_promotion_ready_v1
// CHECK: llvm.call @__obelisk_eval_group_promotion_scan_v1
// CHECK-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// CHECK-SAME: schedule.eval.control_flow_dataflow
// CHECK-SAME: schedule.eval.group_proof_domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [1, 2, 3, 4, 5, 6, 7, 8], ranges = [8, 72]>
// CHECK: llvm.call @__obelisk_eval_group_promotion_ready_v1
// CHECK: llvm.cond_br
// CHECK: schedule.eval.activation_entry = 1 : i32
// CHECK: llvm.cond_br
// CHECK: schedule.eval.activation_entry = 8 : i32
// CHECK: llvm.cond_br
// CHECK-NOT: __obelisk_eval_kernel_promotion_ready_v1
// CHECK-LABEL: llvm.func @__obelisk_eval_ranked_group_0.fallback(
// CHECK: llvm.call @__obelisk_eval_kernel_promotion_ready_v1

!ref = !simulation.ref<i1>
!word = !simulation.logic<8>
!vref = !simulation.ref<!word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, schedule.native.max_inline_ops = 50000 : i64, native.debug_timing} {
  simulation.design @forward_chain {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 2 in 0 : !word design
    simulation.storage.decl 3 in 0 : !word design
    simulation.storage.decl 4 in 0 : !word design
    simulation.storage.decl 5 in 0 : !word design
    simulation.storage.decl 6 in 0 : !word design
    simulation.storage.decl 7 in 0 : !word design
    simulation.storage.decl 8 in 0 : !word design
    simulation.storage.decl 9 in 0 : !word design
    simulation.storage.decl 10 in 0 : !word design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "step1"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "link0"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "link1"
    simulation.code_unit.decl 6 in 0 continuous hierarchy "link2"
    simulation.code_unit.decl 7 in 0 continuous hierarchy "link3"
    simulation.code_unit.decl 8 in 0 continuous hierarchy "link4"
    simulation.code_unit.decl 9 in 0 continuous hierarchy "link5"
    simulation.code_unit.decl 10 in 0 continuous hierarchy "link6"
    simulation.code_unit.decl 11 in 0 continuous hierarchy "link7"
    simulation.code_unit.decl 20 in 0 initial hierarchy "check"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %s1 = simulation.context.storage %ctx[2] : !vref
      %zero = arith.constant false
      simulation.ref.store %zero to %clk : i1, !ref
      %initial = simulation.logic.constant 0 : i8, 0 : i8 : !word
      simulation.ref.store %initial to %s1 : !word, !vref
      %counter = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %n0 = arith.constant 0 : i8
      simulation.ref.store %n0 to %counter : i8, !simulation.ref<i8>
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %in0 = simulation.context.storage %ctx[2] : !vref
      %out0 = simulation.context.storage %ctx[3] : !vref
      %link0 = simulation.spawn @link0(%ctx, %in0, %out0) : !simulation.context, !vref, !vref -> !simulation.process
      %in1 = simulation.context.storage %ctx[3] : !vref
      %out1 = simulation.context.storage %ctx[4] : !vref
      %link1 = simulation.spawn @link1(%ctx, %in1, %out1) : !simulation.context, !vref, !vref -> !simulation.process
      %in2 = simulation.context.storage %ctx[4] : !vref
      %out2 = simulation.context.storage %ctx[5] : !vref
      %link2 = simulation.spawn @link2(%ctx, %in2, %out2) : !simulation.context, !vref, !vref -> !simulation.process
      %in3 = simulation.context.storage %ctx[5] : !vref
      %out3 = simulation.context.storage %ctx[6] : !vref
      %link3 = simulation.spawn @link3(%ctx, %in3, %out3) : !simulation.context, !vref, !vref -> !simulation.process
      %in4 = simulation.context.storage %ctx[6] : !vref
      %out4 = simulation.context.storage %ctx[7] : !vref
      %link4 = simulation.spawn @link4(%ctx, %in4, %out4) : !simulation.context, !vref, !vref -> !simulation.process
      %in5 = simulation.context.storage %ctx[7] : !vref
      %out5 = simulation.context.storage %ctx[8] : !vref
      %link5 = simulation.spawn @link5(%ctx, %in5, %out5) : !simulation.context, !vref, !vref -> !simulation.process
      %in6 = simulation.context.storage %ctx[8] : !vref
      %out6 = simulation.context.storage %ctx[9] : !vref
      %link6 = simulation.spawn @link6(%ctx, %in6, %out6) : !simulation.context, !vref, !vref -> !simulation.process
      %in7 = simulation.context.storage %ctx[9] : !vref
      %out7 = simulation.context.storage %ctx[10] : !vref
      %link7 = simulation.spawn @link7(%ctx, %in7, %out7) : !simulation.context, !vref, !vref -> !simulation.process
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

    simulation.func @step1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %input to ^body {site = #schedule.continuation<id = 2>} : !ref
    ^body:
      %counter = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %n = simulation.ref.load %counter : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %nextn = arith.addi %n, %one : i8
      simulation.ref.store %nextn to %counter : i8, !simulation.ref<i8>
      %twice = arith.addi %nextn, %nextn : i8
      %bits = simulation.logic.from_bits %twice : i8 -> !word
      %x = simulation.logic.constant 0 : i8, 255 : i8 : !word
      %z = simulation.logic.constant 255 : i8, 255 : i8 : !word
      %two = arith.constant 2 : i8
      %three = arith.constant 3 : i8
      %atx = arith.cmpi eq, %nextn, %two : i8
      %atz = arith.cmpi eq, %nextn, %three : i8
      %withx = arith.select %atx, %x, %bits : !word
      %value = arith.select %atz, %z, %withx : !word
      simulation.ref.store %value to %output : !word, !vref
      // The observer remains an ordered source boundary outside the pure group.
      %fmt = simulation.bytes.constant "driver %0b"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %value) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !word
      cf.br ^wait
    }
    simulation.func @link0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @link1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @link2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 6 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @link3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 7 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @link4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 8 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @link5(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 8 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 9 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @link6(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 8 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 9 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 10 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @link7(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 9 : i64}, %output: !vref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 10 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 11 : i64} {
      cf.br ^body
    ^wait:
      simulation.suspend.change %input to ^body : !vref
    ^body:
      %v = simulation.ref.load %input : !vref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %v, %one : !word
      simulation.ref.store %next to %output : !word, !vref
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 20 : i64} {
      %output = simulation.context.storage %ctx[10] : !vref
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^report0
    ^report0:
      %value0 = simulation.ref.load %output : !vref -> !word
      %fmt0 = simulation.bytes.constant "chain %0d"
      %channel0 = arith.constant 1 : i32
      simulation.display %ctx to %channel0(%fmt0, %value0) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %delay0 = simulation.time.constant 2
      simulation.suspend.delay %delay0 to ^report1
    ^report1:
      %value1 = simulation.ref.load %output : !vref -> !word
      %fmt1 = simulation.bytes.constant "chain %0d"
      %channel1 = arith.constant 1 : i32
      simulation.display %ctx to %channel1(%fmt1, %value1) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %delay1 = simulation.time.constant 2
      simulation.suspend.delay %delay1 to ^report2
    ^report2:
      %value2 = simulation.ref.load %output : !vref -> !word
      %fmt2 = simulation.bytes.constant "chain %0d"
      %channel2 = arith.constant 1 : i32
      simulation.display %ctx to %channel2(%fmt2, %value2) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %delay2 = simulation.time.constant 2
      simulation.suspend.delay %delay2 to ^report3
    ^report3:
      %value3 = simulation.ref.load %output : !vref -> !word
      %fmt3 = simulation.bytes.constant "chain %0d"
      %channel3 = arith.constant 1 : i32
      simulation.display %ctx to %channel3(%fmt3, %value3) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %delay3 = simulation.time.constant 2
      simulation.suspend.delay %delay3 to ^report4
    ^report4:
      %value4 = simulation.ref.load %output : !vref -> !word
      %fmt4 = simulation.bytes.constant "chain %0d"
      %channel4 = arith.constant 1 : i32
      simulation.display %ctx to %channel4(%fmt4, %value4) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %delay4 = simulation.time.constant 2
      simulation.suspend.delay %delay4 to ^report5
    ^report5:
      %value5 = simulation.ref.load %output : !vref -> !word
      %fmt5 = simulation.bytes.constant "chain %0d"
      %channel5 = arith.constant 1 : i32
      simulation.display %ctx to %channel5(%fmt5, %value5) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %done = arith.constant 0 : i32
      simulation.finish %ctx, %done
      simulation.return
    }
  }
}
