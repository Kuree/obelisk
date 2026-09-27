// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba))' -o %t.sim.mlir
// RUN: obelisk-opt %t.sim.mlir --encode-obelisk-sim-to-bytecode --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.native.mlir
// RUN: FileCheck %s --check-prefix=IR < %t.native.mlir

// Runtime behavior is checked in ../Runtime/simulation-scoped-force-state-runtime.test.

// LRM 10.6.2: a forced procedural variable ignores writes and retains its
// forced value on release until the next procedural assignment. The unrelated
// root must remain directly addressable during and after the force.
// IR-LABEL: llvm.func @unrelated(
// IR-NOT: llvm.call @obelisk_rt_v1_native_state_{{load|store}}_plane
// IR-NOT: llvm.call @obelisk_rt_v1_static_specialization_guard
// IR: llvm.return
// IR-LABEL: llvm.func @read_guarded(
// IR: llvm.call @obelisk_rt_v1_static_specialization_guard
// IR: llvm.call @obelisk_rt_v1_native_state_load_plane
// IR: llvm.return

!word = !simulation.logic<4>
!ref = !simulation.ref<!word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @scoped_force {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "test"
    simulation.code_unit.decl 3 in 0 function hierarchy "unrelated"
    simulation.code_unit.decl 4 in 0 function hierarchy "read_guarded"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %p = simulation.spawn @test(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %forced = simulation.context.storage %ctx[0] : !ref
      %stdout = arith.constant 1 : i32
      %initial = simulation.call @read_guarded(%ctx) : (!simulation.context) -> !word
      %initfmt = simulation.bytes.constant "initial=%h"
      simulation.display %ctx to %stdout(%initfmt, %initial) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      %z = simulation.logic.constant 15 : i4, 15 : i4 : !word
      %nine = simulation.logic.constant 9 : i4, 0 : i4 : !word
      %five = simulation.logic.constant 5 : i4, 0 : i4 : !word
      simulation.override %forced = %z assign false : !ref, !word
      simulation.ref.store %nine to %forced : !word, !ref
      %other = simulation.call @unrelated(%ctx, %nine) : (!simulation.context, !word) -> !word
      %held = simulation.call @read_guarded(%ctx) : (!simulation.context) -> !word
      %forcefmt = simulation.bytes.constant "forced=%h unrelated=%h"
      simulation.display %ctx to %stdout(%forcefmt, %held, %other) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !word, !word
      simulation.release_override %forced assign false : !ref
      %other2 = simulation.call @unrelated(%ctx, %five) : (!simulation.context, !word) -> !word
      %released = simulation.call @read_guarded(%ctx) : (!simulation.context) -> !word
      %releasefmt = simulation.bytes.constant "released=%h unrelated=%h"
      simulation.display %ctx to %stdout(%releasefmt, %released, %other2) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !word, !word
      simulation.ref.store %five to %forced : !word, !ref
      %written = simulation.call @read_guarded(%ctx) : (!simulation.context) -> !word
      %writefmt = simulation.bytes.constant "written=%h"
      simulation.display %ctx to %stdout(%writefmt, %written) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !word
      simulation.return
    }
    simulation.func @unrelated(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !word {simulation.capture_kind = 2 : i32}) -> !word attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[1] : !ref
      simulation.ref.store %value to %root : !word, !ref
      %result = simulation.ref.load %root : !ref -> !word
      simulation.return %result : !word
    }
    simulation.func @read_guarded(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !word attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = simulation.context.storage %ctx[0] : !ref
      %result = simulation.ref.load %root : !ref -> !word
      simulation.return %result : !word
    }
  }
}
