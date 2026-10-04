// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir > %t.ll
// RUN: opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' %t.ll | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' %t.ll | %llc -filetype=obj -relocation-model=pic -o %t.o0
// RUN: %llvm_dist/bin/clang++ %t.o0 %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: %t.o0.exe --execution-tier=native | FileCheck %s
// RUN: %t.o0.exe --execution-tier=bytecode | FileCheck %s

// LRM 9.4.2: pending generated readiness can coalesce, but a newly registered
// runtime observer must still receive a transient posedge. Three bursts run
// before, during and after that observer's lifetime. The middle burst ends
// at zero; looking only at its final value would miss the observer's wakeup.
// The generated inverter chain must see each burst's final canonical state.
// CHECK: transient watcher=0
// CHECK-NEXT: reset=1 inverted=0 forwarded=1 sampled=1
// PLAN: __obelisk_periodic_clock_plan_v1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @startup_publication {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.scope.decl 2 parent 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 1 continuous hierarchy "first"
    simulation.code_unit.decl 4 in 2 continuous hierarchy "second"
    simulation.code_unit.decl 5 in 0 always hierarchy "sample"
    simulation.code_unit.decl 6 in 0 initial hierarchy "observe"
    simulation.code_unit.decl 7 in 0 function hierarchy "fill"
    simulation.code_unit.decl 8 in 0 initial hierarchy "watch"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design
    simulation.storage.decl 4 in 0 : !simulation.logic<1> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %s0 = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %s1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %s2 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %s3 = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<1>>
      %s4 = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %bit = arith.constant false
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.ref.store %bit to %s0 : i1, !simulation.ref<i1>
      simulation.ref.store %zero to %s1 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %zero to %s2 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %zero to %s3 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %zero to %s4 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %clock = simulation.spawn @clock(%ctx, %s0) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %first = simulation.spawn @first(%ctx, %s1, %s2) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %second = simulation.spawn @second(%ctx, %s2, %s3) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %sample = simulation.spawn @sample(%ctx, %s0, %s3, %s4) : !simulation.context, !simulation.ref<i1>, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %watch = simulation.spawn @watch(%ctx) : !simulation.context -> !simulation.process
      %observe = simulation.spawn @observe(%ctx, %s1, %s2, %s3, %s4) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %s0: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %s0 : !simulation.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %s0 : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %s1: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %s2: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      cf.br ^run
    ^run:
      %old = simulation.ref.load %s1 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %s2 {simulation.continuous_store} : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %s1 to ^run {site = #schedule.continuation<id = 3>} : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %s2: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %s3: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      cf.br ^run
    ^run:
      %old = simulation.ref.load %s2 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %s3 {simulation.continuous_store} : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %s2 to ^run {site = #schedule.continuation<id = 4>} : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %s0: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %s3: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
        %s4: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %s0 to ^run {site = #schedule.continuation<id = 5>} : !simulation.ref<i1>
    ^run:
      %value = simulation.ref.load %s3 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.nba.enqueue %value to %s4 : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      cf.br ^wait
    }
    simulation.func @observe(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %s1: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %s2: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %s3: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
        %s4: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %d0 = simulation.time.constant 1
      simulation.suspend.delay %d0 to ^first
    ^first:
      simulation.call @fill(%ctx) : (!simulation.context) -> ()
      %d1 = simulation.time.constant 3
      simulation.suspend.delay %d1 to ^second
    ^second:
      simulation.call @fill(%ctx) : (!simulation.context) -> ()
      %d2 = simulation.time.constant 3
      simulation.suspend.delay %d2 to ^third
    ^third:
      simulation.call @fill(%ctx) : (!simulation.context) -> ()
      %d3 = simulation.time.constant 3
      simulation.suspend.delay %d3 to ^show
    ^show:
      %v1 = simulation.ref.load %s1 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v2 = simulation.ref.load %s2 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v3 = simulation.ref.load %s3 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v4 = simulation.ref.load %s4 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %fmt = simulation.bytes.constant "reset=%b inverted=%b forwarded=%b sampled=%b"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %v1, %v2, %v3, %v4) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0] : !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>
      simulation.finish %ctx, %channel
      simulation.return
    }
    simulation.func @fill(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %zero = arith.constant 0 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%iteration: i32):
      %old = simulation.ref.load %root : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %root : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %limit = arith.constant 257 : i32
      %count = arith.addi %iteration, %one : i32
      %more = arith.cmpi ult, %count, %limit : i32
      cf.cond_br %more, ^loop(%count : i32), ^done
    ^done:
      simulation.return
    }
    simulation.func @watch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^arm
    ^arm:
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      simulation.suspend.edge posedge %root to ^print : !simulation.ref<!simulation.logic<1>>
    ^print:
      %value = simulation.ref.load %root : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %format = simulation.bytes.constant "transient watcher=%b"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%format, %value) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      simulation.return
    }
  }
}
