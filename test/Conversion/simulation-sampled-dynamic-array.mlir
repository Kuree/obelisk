// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o %t.sim.mlir
// RUN: obelisk-opt %t.sim.mlir --encode-obelisk-sim-to-bytecode --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.native.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.native.mlir

// Runtime behavior is checked in ../Runtime/simulation-sampled-dynamic-array.test.

// The selector and array are sampled before the Active writes. The snapshot
// contains the proven array roots, excluding unrelated storage. An invalid
// lane is X for four-state data and zero for two-state data (LRM 7.4.5, 16.5.1).
// PLAN: obelisk.execution.sampled_ranges = array<i64: 0, 40, 168, 8>
!word = !simulation.logic<4>
!array = !simulation.unpacked_array<1 : 2 x !word>
!two = !simulation.unpacked_array<1 : 2 x i4>
!index = !simulation.logic<32>
!aref = !simulation.ref<!array>
!wref = !simulation.ref<!word>
!iref = !simulation.ref<!index>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @sampled_array {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : !index design
    simulation.storage.decl 2 in 0 : i128 design
    simulation.storage.decl 3 in 0 : !two design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "test"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %test = simulation.spawn @test(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %array = simulation.context.storage %ctx[0] : !aref
      %index = simulation.context.storage %ctx[1] : !iref
      %two = simulation.context.storage %ctx[3] : !simulation.ref<!two>
      %six = simulation.logic.constant 6 : i4, 0 : i4 : !word
      %z = simulation.logic.constant 15 : i4, 15 : i4 : !word
      %nine = simulation.logic.constant 9 : i4, 0 : i4 : !word
      %initial = simulation.aggregate.construct %six, %z : (!word, !word) -> !array
      simulation.ref.store %initial to %array : !array, !aref
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !index
      %second = simulation.logic.constant 2 : i32, 0 : i32 : !index
      %bad = simulation.logic.constant 3 : i32, 0 : i32 : !index
      %x = simulation.logic.constant 0 : i32, -1 : i32 : !index
      simulation.ref.store %one to %index : !index, !iref
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^observe
    ^observe:
      %changed = simulation.aggregate.construct %nine, %nine : (!word, !word) -> !array
      simulation.ref.store %changed to %array : !array, !aref
      simulation.ref.store %second to %index : !index, !iref
      %selected = simulation.assert.sampled_read %ctx from %index : (!simulation.context, !iref) -> !index
      %lane = simulation.ref.array_element %array[%selected] : (!aref, !index) -> !wref
      %last = simulation.ref.array_element %array[%second] : (!aref, !index) -> !wref
      %invalid = simulation.ref.array_element %array[%bad] : (!aref, !index) -> !wref
      %unknown = simulation.ref.array_element %array[%x] : (!aref, !index) -> !wref
      %invalidtwo = simulation.ref.array_element %two[%bad] : (!simulation.ref<!two>, !index) -> !simulation.ref<i4>
      %v = simulation.assert.sampled_read %ctx from %lane : (!simulation.context, !wref) -> !word
      %vz = simulation.assert.sampled_read %ctx from %last : (!simulation.context, !wref) -> !word
      %vi = simulation.assert.sampled_read %ctx from %invalid : (!simulation.context, !wref) -> !word
      %vx = simulation.assert.sampled_read %ctx from %unknown : (!simulation.context, !wref) -> !word
      %vt = simulation.assert.sampled_read %ctx from %invalidtwo : (!simulation.context, !simulation.ref<i4>) -> i4
      %channel = arith.constant 1 : i32
      %fmt = simulation.bytes.constant "sampled=%h z=%h invalid=%h unknown=%h two=%h"
      simulation.display %ctx to %channel(%fmt, %v, %vz, %vi, %vx, %vt) newline = true radix = <hex> flags = [0, 0, 0, 0, 0, 0] : !simulation.bytes, !word, !word, !word, !word, i4
      simulation.suspend.delay %delay to ^again
    ^again:
      %now = simulation.assert.sampled_read %ctx from %index : (!simulation.context, !iref) -> !index
      %nextlane = simulation.ref.array_element %array[%now] : (!aref, !index) -> !wref
      %next = simulation.assert.sampled_read %ctx from %nextlane : (!simulation.context, !wref) -> !word
      %fmt2 = simulation.bytes.constant "next=%h"
      simulation.display %ctx to %channel(%fmt2, %next) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !word
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
