// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba))' -o %t.sim.mlir
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

!word = !obelisk_sim.logic<4>
!ref = !obelisk_sim.ref<!word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk_sim.design @scoped_force {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !word design
    obelisk_sim.storage.decl 1 in 0 : !word design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "test"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "unrelated"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "read_guarded"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %p = obelisk_sim.spawn @test(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @test(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %forced = obelisk_sim.context.storage %ctx[0] : !ref
      %stdout = arith.constant 1 : i32
      %initial = obelisk_sim.call @read_guarded(%ctx) : (!obelisk_sim.context) -> !word
      %initfmt = obelisk_sim.bytes.constant "initial=%h"
      obelisk_sim.display %ctx to %stdout(%initfmt, %initial) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !word
      %z = obelisk_sim.logic.constant 15 : i4, 15 : i4 : !word
      %nine = obelisk_sim.logic.constant 9 : i4, 0 : i4 : !word
      %five = obelisk_sim.logic.constant 5 : i4, 0 : i4 : !word
      obelisk_sim.override %forced = %z assign false : !ref, !word
      obelisk_sim.ref.store %nine to %forced : !word, !ref
      %other = obelisk_sim.call @unrelated(%ctx, %nine) : (!obelisk_sim.context, !word) -> !word
      %held = obelisk_sim.call @read_guarded(%ctx) : (!obelisk_sim.context) -> !word
      %forcefmt = obelisk_sim.bytes.constant "forced=%h unrelated=%h"
      obelisk_sim.display %ctx to %stdout(%forcefmt, %held, %other) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !word, !word
      obelisk_sim.release_override %forced assign false : !ref
      %other2 = obelisk_sim.call @unrelated(%ctx, %five) : (!obelisk_sim.context, !word) -> !word
      %released = obelisk_sim.call @read_guarded(%ctx) : (!obelisk_sim.context) -> !word
      %releasefmt = obelisk_sim.bytes.constant "released=%h unrelated=%h"
      obelisk_sim.display %ctx to %stdout(%releasefmt, %released, %other2) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !word, !word
      obelisk_sim.ref.store %five to %forced : !word, !ref
      %written = obelisk_sim.call @read_guarded(%ctx) : (!obelisk_sim.context) -> !word
      %writefmt = obelisk_sim.bytes.constant "written=%h"
      obelisk_sim.display %ctx to %stdout(%writefmt, %written) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !word
      obelisk_sim.return
    }
    obelisk_sim.func @unrelated(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: !word {obelisk_sim.capture_kind = 2 : i32}) -> !word attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = obelisk_sim.context.storage %ctx[1] : !ref
      obelisk_sim.ref.store %value to %root : !word, !ref
      %result = obelisk_sim.ref.load %root : !ref -> !word
      obelisk_sim.return %result : !word
    }
    obelisk_sim.func @read_guarded(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> !word attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = obelisk_sim.context.storage %ctx[0] : !ref
      %result = obelisk_sim.ref.load %root : !ref -> !word
      obelisk_sim.return %result : !word
    }
  }
}
