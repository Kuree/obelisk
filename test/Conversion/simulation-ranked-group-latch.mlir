// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe --execution-tier=native 2> %t.native.err | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe --execution-tier=bytecode 2> %t.bytecode.err | FileCheck %s
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o0
// RUN: %llvm_dist/bin/clang++ %t.o0 %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.o0.exe --execution-tier=native 2> %t.native.err | FileCheck %s
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o0
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o0 %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.o0.exe
// RUN: %t.bytecode.o0.exe --execution-tier=bytecode 2> %t.bytecode.err | FileCheck %s

// IEEE 1800-2023 9.2.2.3: a closed latch holds its last assigned
// bits, including untouched X bits. Data changes while closed; reopening
// captures the new data. The latch shares a ranked helper with its neighbors;
// its keyword alone must not force an executor downgrade.
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: obelisk.eval.ranked_members = array<i32: 0, 1, 2, 3>
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0
// CHECK: latch xxxxxxxx
// CHECK-NEXT: latch xxxx0010
// CHECK-NEXT: latch xxxx0101
// CHECK-NEXT: latch xxxx0101
// CHECK-NEXT: latch xxxx0111
!ref = !obelisk_sim.ref<i1>
!vref = !obelisk_sim.ref<i8>
!word = !obelisk_sim.logic<8>
!lref = !obelisk_sim.ref<!word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @forward_chain {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i8 design
    obelisk_sim.storage.decl 2 in 0 : i8 design
    obelisk_sim.storage.decl 3 in 0 : !word design
    obelisk_sim.storage.decl 4 in 0 : !word design
    obelisk_sim.storage.decl 5 in 0 : !word design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "step1"
    obelisk_sim.code_unit.decl 4 in 0 continuous hierarchy "step2"
    obelisk_sim.code_unit.decl 5 in 0 always_latch hierarchy "step3"
    obelisk_sim.code_unit.decl 6 in 0 continuous hierarchy "step4"
    obelisk_sim.code_unit.decl 7 in 0 continuous hierarchy "step5"
    obelisk_sim.code_unit.decl 8 in 0 initial hierarchy "check"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !ref
      %s1 = obelisk_sim.context.storage %ctx[1] : !vref
      %s2 = obelisk_sim.context.storage %ctx[2] : !vref
      %s3 = obelisk_sim.context.storage %ctx[3] : !lref
      %s4 = obelisk_sim.context.storage %ctx[4] : !lref
      %s5 = obelisk_sim.context.storage %ctx[5] : !lref
      %zero = arith.constant false
      obelisk_sim.ref.store %zero to %clk : i1, !ref
      %clock = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !ref -> !obelisk_sim.process
      %p5 = obelisk_sim.spawn @step5(%ctx, %s4, %s5) : !obelisk_sim.context, !lref, !lref -> !obelisk_sim.process
      %p4 = obelisk_sim.spawn @step4(%ctx, %s3, %s4) : !obelisk_sim.context, !lref, !lref -> !obelisk_sim.process
      %p3 = obelisk_sim.spawn @step3(%ctx, %s2, %s3, %clk) : !obelisk_sim.context, !vref, !lref, !ref -> !obelisk_sim.process
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
      %wide = obelisk_sim.ref.load %output : !vref -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %wide, %one : i8
      obelisk_sim.ref.store %next to %output : i8, !vref
      cf.br ^wait
    }
    obelisk_sim.func @step2(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %output: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^wait:
      obelisk_sim.suspend.change %input to ^body {site = #obelisk_sim.continuation<id = 3>} : !vref
    ^body:
      %v = obelisk_sim.ref.load %input : !vref -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %v, %one : i8
      obelisk_sim.ref.store %next to %output : i8, !vref
      cf.br ^wait
    }
    obelisk_sim.func @step3(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %output: !lref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %enable: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 6 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^wait:
      obelisk_sim.suspend.any %input, %enable edges [0, 0] to ^body {site = #obelisk_sim.continuation<id = 4>} : !vref, !ref
    ^body:
      %open = obelisk_sim.ref.load %enable : !ref -> i1
      cf.cond_br %open, ^assign, ^wait
    ^assign:
      %value = obelisk_sim.ref.load %input : !vref -> i8
      %bits = arith.trunci %value : i8 to i4
      %data = obelisk_sim.logic.from_bits %bits : i4 -> !obelisk_sim.logic<4>
      %low = obelisk_sim.ref.extract %output from 0 : !lref -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      obelisk_sim.ref.store %data to %low : !obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>
      cf.br ^wait
    }
    obelisk_sim.func @step4(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !lref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %output: !lref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 6 : i64} {
      cf.br ^body
    ^wait:
      obelisk_sim.suspend.change %input to ^body {site = #obelisk_sim.continuation<id = 5>} : !lref
    ^body:
      %v = obelisk_sim.ref.load %input : !lref -> !word
      obelisk_sim.ref.store %v to %output : !word, !lref
      cf.br ^wait
    }
    obelisk_sim.func @step5(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !lref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}, %output: !lref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 5 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 7 : i64} {
      cf.br ^body
    ^wait:
      obelisk_sim.suspend.change %input to ^body {site = #obelisk_sim.continuation<id = 6>} : !lref
    ^body:
      %v = obelisk_sim.ref.load %input : !lref -> !word
      obelisk_sim.ref.store %v to %output : !word, !lref
      cf.br ^wait
    }
    obelisk_sim.func @check(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %output = obelisk_sim.context.storage %ctx[5] : !lref
      %first = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %first to ^closed
    ^closed:
      %a = obelisk_sim.ref.load %output : !lref -> !word
      %fmt0 = obelisk_sim.bytes.constant "latch %b"
      %channel0 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel0(%fmt0, %a) newline = true radix = 2 flags = [0, 0] : !obelisk_sim.bytes, !word
      %next0 = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %next0 to ^opened
    ^opened:
      %b = obelisk_sim.ref.load %output : !lref -> !word
      %fmt1 = obelisk_sim.bytes.constant "latch %b"
      %channel1 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel1(%fmt1, %b) newline = true radix = 2 flags = [0, 0] : !obelisk_sim.bytes, !word
      %source = obelisk_sim.context.storage %ctx[1] : !vref
      %four = arith.constant 4 : i8
      obelisk_sim.ref.store %four to %source : i8, !vref
      %delta = obelisk_sim.time.constant 0
      obelisk_sim.suspend.delay %delta to ^transparent
    ^transparent:
      %changed = obelisk_sim.ref.load %output : !lref -> !word
      %fmtChanged = obelisk_sim.bytes.constant "latch %b"
      %channelChanged = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channelChanged(%fmtChanged, %changed) newline = true radix = 2 flags = [0, 0] : !obelisk_sim.bytes, !word
      %next1 = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %next1 to ^held
    ^held:
      %c = obelisk_sim.ref.load %output : !lref -> !word
      %fmt2 = obelisk_sim.bytes.constant "latch %b"
      %channel2 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel2(%fmt2, %c) newline = true radix = 2 flags = [0, 0] : !obelisk_sim.bytes, !word
      %next2 = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %next2 to ^reopened
    ^reopened:
      %d = obelisk_sim.ref.load %output : !lref -> !word
      %fmt3 = obelisk_sim.bytes.constant "latch %b"
      %channel3 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel3(%fmt3, %d) newline = true radix = 2 flags = [0, 0] : !obelisk_sim.bytes, !word
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }
  }
}
