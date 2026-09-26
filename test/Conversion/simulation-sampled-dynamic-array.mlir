// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o %t.sim.mlir
// RUN: obelisk-opt %t.sim.mlir --encode-obelisk-sim-to-bytecode --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.native.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.native.mlir

// Runtime behavior is checked in ../Runtime/simulation-sampled-dynamic-array.test.

// The selector and array are sampled before the Active writes. The snapshot
// contains the proven array roots, excluding unrelated storage. An invalid
// lane is X for four-state data and zero for two-state data (LRM 7.4.5, 16.5.1).
// PLAN: obelisk.execution.sampled_ranges = array<i64: 0, 40, 168, 8>
!word = !obelisk_sim.logic<4>
!array = !obelisk_sim.unpacked_array<1 : 2 x !word>
!two = !obelisk_sim.unpacked_array<1 : 2 x i4>
!index = !obelisk_sim.logic<32>
!aref = !obelisk_sim.ref<!array>
!wref = !obelisk_sim.ref<!word>
!iref = !obelisk_sim.ref<!index>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk_sim.design @sampled_array {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !array design
    obelisk_sim.storage.decl 1 in 0 : !index design
    obelisk_sim.storage.decl 2 in 0 : i128 design
    obelisk_sim.storage.decl 3 in 0 : !two design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "test"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %test = obelisk_sim.spawn @test(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @test(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %array = obelisk_sim.context.storage %ctx[0] : !aref
      %index = obelisk_sim.context.storage %ctx[1] : !iref
      %two = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!two>
      %six = obelisk_sim.logic.constant 6 : i4, 0 : i4 : !word
      %z = obelisk_sim.logic.constant 15 : i4, 15 : i4 : !word
      %nine = obelisk_sim.logic.constant 9 : i4, 0 : i4 : !word
      %initial = obelisk_sim.aggregate.construct %six, %z : (!word, !word) -> !array
      obelisk_sim.ref.store %initial to %array : !array, !aref
      %one = obelisk_sim.logic.constant 1 : i32, 0 : i32 : !index
      %second = obelisk_sim.logic.constant 2 : i32, 0 : i32 : !index
      %bad = obelisk_sim.logic.constant 3 : i32, 0 : i32 : !index
      %x = obelisk_sim.logic.constant 0 : i32, -1 : i32 : !index
      obelisk_sim.ref.store %one to %index : !index, !iref
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^observe
    ^observe:
      %changed = obelisk_sim.aggregate.construct %nine, %nine : (!word, !word) -> !array
      obelisk_sim.ref.store %changed to %array : !array, !aref
      obelisk_sim.ref.store %second to %index : !index, !iref
      %selected = obelisk_sim.assert.sampled_read %ctx from %index : (!obelisk_sim.context, !iref) -> !index
      %lane = obelisk_sim.ref.array_element %array[%selected] : (!aref, !index) -> !wref
      %last = obelisk_sim.ref.array_element %array[%second] : (!aref, !index) -> !wref
      %invalid = obelisk_sim.ref.array_element %array[%bad] : (!aref, !index) -> !wref
      %unknown = obelisk_sim.ref.array_element %array[%x] : (!aref, !index) -> !wref
      %invalidtwo = obelisk_sim.ref.array_element %two[%bad] : (!obelisk_sim.ref<!two>, !index) -> !obelisk_sim.ref<i4>
      %v = obelisk_sim.assert.sampled_read %ctx from %lane : (!obelisk_sim.context, !wref) -> !word
      %vz = obelisk_sim.assert.sampled_read %ctx from %last : (!obelisk_sim.context, !wref) -> !word
      %vi = obelisk_sim.assert.sampled_read %ctx from %invalid : (!obelisk_sim.context, !wref) -> !word
      %vx = obelisk_sim.assert.sampled_read %ctx from %unknown : (!obelisk_sim.context, !wref) -> !word
      %vt = obelisk_sim.assert.sampled_read %ctx from %invalidtwo : (!obelisk_sim.context, !obelisk_sim.ref<i4>) -> i4
      %channel = arith.constant 1 : i32
      %fmt = obelisk_sim.bytes.constant "sampled=%h z=%h invalid=%h unknown=%h two=%h"
      obelisk_sim.display %ctx to %channel(%fmt, %v, %vz, %vi, %vx, %vt) newline = true radix = 16 flags = [0, 0, 0, 0, 0, 0] : !obelisk_sim.bytes, !word, !word, !word, !word, i4
      obelisk_sim.suspend.delay %delay to ^again
    ^again:
      %now = obelisk_sim.assert.sampled_read %ctx from %index : (!obelisk_sim.context, !iref) -> !index
      %nextlane = obelisk_sim.ref.array_element %array[%now] : (!aref, !index) -> !wref
      %next = obelisk_sim.assert.sampled_read %ctx from %nextlane : (!obelisk_sim.context, !wref) -> !word
      %fmt2 = obelisk_sim.bytes.constant "next=%h"
      obelisk_sim.display %ctx to %channel(%fmt2, %next) newline = true radix = 16 flags = [0, 0] : !obelisk_sim.bytes, !word
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }
  }
}
