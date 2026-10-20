// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | obelisk-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.native.o
// RUN: %target_cxx %t.native.o %runtime_link_flags -o %t.native.exe
// RUN: %t.native.exe | FileCheck %s
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | obelisk-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %target_cxx %t.bytecode.o %runtime_link_flags -o %t.bytecode.exe
// RUN: %t.bytecode.exe | FileCheck %s

// IEEE 1800-2023 4.4.2.3: `copy` resumes from its edge wait in the Inactive
// region, as `@(posedge clk) #0` would, so it runs after every Active event
// of the edge, including `count`'s blocking increment. It is spawned first,
// and without the resume region the runtime runs it before `count` and it
// trails by one (10 9).

!byte = !simulation.logic<8>
!bit = !simulation.logic<1>
!bitref = !simulation.ref<!bit>
!byteref = !simulation.ref<!byte>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @inactive {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.storage.decl 2 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "copy"
    simulation.code_unit.decl 4 in 0 always hierarchy "count"
    simulation.code_unit.decl 5 in 0 final hierarchy "last"
    simulation.code_unit.decl 6 in 0 initial hierarchy "stop"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %cnt = simulation.context.storage %ctx[1] : !byteref
      %seen = simulation.context.storage %ctx[2] : !byteref
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !byte
      simulation.ref.store %zero to %cnt : !byte, !byteref
      simulation.ref.store %zero to %seen : !byte, !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @copy(%ctx, %clk, %cnt, %seen) : !simulation.context, !bitref, !byteref, !byteref -> !simulation.process
      %p2 = simulation.spawn @count(%ctx, %clk, %cnt) : !simulation.context, !bitref, !byteref -> !simulation.process
      %p3 = simulation.spawn @last(%ctx) : !simulation.context -> !simulation.process
      %p4 = simulation.spawn @stop(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.ref.store %one to %clk : !bit, !bitref
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.ref.store %zero to %clk : !bit, !bitref
      cf.br ^low
    }
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %cnt: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %seen: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {resume_region = 3 : i32} : !bitref
    ^body:
      %v = simulation.ref.load %cnt : !byteref -> !byte
      simulation.ref.store %v to %seen : !byte, !byteref
      cf.br ^wait
    }
    simulation.func @count(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %cnt: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %v = simulation.ref.load %cnt : !byteref -> !byte
      %n = simulation.logic.binary add %v, %one : !byte
      simulation.ref.store %n to %cnt : !byte, !byteref
      cf.br ^wait
    }
    simulation.func @stop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 5 : i64} {
      %cnt = simulation.context.storage %ctx[1] : !byteref
      %seen = simulation.context.storage %ctx[2] : !byteref
      %c = simulation.ref.load %cnt : !byteref -> !byte
      %s = simulation.ref.load %seen : !byteref -> !byte
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%0d %0d"
      simulation.display %ctx to %stdout(%format, %c, %s) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !byte, !byte
      simulation.return
    }
  }
}
// CHECK: {{^}}10 10{{$}}
