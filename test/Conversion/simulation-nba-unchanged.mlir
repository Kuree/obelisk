// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2>&1 | FileCheck %s --check-prefix=PROOF
// RUN: FileCheck %s --check-prefix=LOWER < %t.mlir

// Runtime coverage in ../Runtime/simulation-nba-unchanged.test.
// An equal-value NBA is inert only if no earlier NBA is pending and no
// blocking writer can intervene. Equality includes the X/Z plane, and a
// partial store must ignore its neighboring bits. LRM 4.3, 4.6, 4.9.4, 10.4.2.
// PROOF: obelisk native unchanged NBA roots: 2/3
// LOWER-LABEL: llvm.func @writer.__obelisk_coro_ramp(
// LOWER: llvm.mlir.addressof @__obelisk_state_value
// LOWER: llvm.mlir.addressof @__obelisk_state_unknown
// LOWER: %[[DELTA:.*]] = llvm.and
// LOWER: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// LOWER: %[[PENDING:.*]] = llvm.and
// LOWER: %[[NEEDED:.*]] = llvm.or %[[DELTA]], %[[PENDING]]
// LOWER: llvm.icmp "ne" %[[NEEDED]],
// LOWER: llvm.cond_br {{.*}}, ^[[STAGE:bb[0-9]+]], ^[[DONE:bb[0-9]+]]
// LOWER-NEXT: ^[[STAGE]]:
// LOWER: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// LOWER: llvm.br ^[[DONE]]

!word = !simulation.logic<13>
!ref = !simulation.ref<!word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, obelisk.native.closed_executable, obelisk.debug.native_timing} {
  simulation.design @unchanged {
    simulation.scope.decl 0 hierarchy "unchanged"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "unchanged.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "unchanged.writer"
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %q = simulation.context.storage %ctx[0] : !ref
      %mixed = simulation.context.storage %ctx[1] : !ref
      %partial = simulation.context.storage %ctx[2] : !ref
      %zero = simulation.logic.constant 0 : i13, 0 : i13 : !word
      %outside = simulation.logic.constant 6151 : i13, 0 : i13 : !word
      simulation.ref.store %zero to %q : !word, !ref
      simulation.ref.store %zero to %mixed : !word, !ref
      simulation.ref.store %outside to %partial : !word, !ref
      %p = simulation.spawn @writer(%ctx, %q, %mixed, %partial) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %mixed: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %partial: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = simulation.logic.constant 0 : i13, 0 : i13 : !word
      %one = simulation.logic.constant 1 : i13, 0 : i13 : !word
      %x = simulation.logic.constant 0 : i13, 8191 : i13 : !word
      %partzero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %partone = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %slice = simulation.ref.extract %partial from 3 : !ref -> !simulation.ref<!simulation.logic<8>>
      // The first update is inert; the third must overwrite a pending one.
      simulation.nba.enqueue %zero to %q : (!word, !ref) -> ()
      simulation.nba.enqueue %one to %q : (!word, !ref) -> ()
      simulation.nba.enqueue %zero to %q : (!word, !ref) -> ()
      // An intervening blocking write makes the equal-value NBA significant.
      simulation.nba.enqueue %zero to %mixed : (!word, !ref) -> ()
      simulation.ref.store %one to %mixed : !word, !ref
      simulation.nba.enqueue %partzero to %slice : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %channel = arith.constant 1 : i32
      %format = simulation.bytes.constant "%h %h %h"
      %a = simulation.ref.load %q : !ref -> !word
      %b = simulation.ref.load %mixed : !ref -> !word
      %c = simulation.ref.load %partial : !ref -> !word
      simulation.display %ctx to %channel(%format, %a, %b, %c) newline = true radix = <hex> flags = [0, 0, 0, 0] : !simulation.bytes, !word, !word, !word
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^first
    ^first:
      %d = simulation.ref.load %q : !ref -> !word
      %e = simulation.ref.load %mixed : !ref -> !word
      %f = simulation.ref.load %partial : !ref -> !word
      simulation.display %ctx to %channel(%format, %d, %e, %f) newline = true radix = <hex> flags = [0, 0, 0, 0] : !simulation.bytes, !word, !word, !word
      // The value bits match zero, but changing known bits to X is not inert.
      simulation.nba.enqueue %x to %q : (!word, !ref) -> ()
      simulation.nba.enqueue %partone to %slice : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.suspend.delay %delay to ^second
    ^second:
      %g = simulation.ref.load %q : !ref -> !word
      %h = simulation.ref.load %mixed : !ref -> !word
      %i = simulation.ref.load %partial : !ref -> !word
      simulation.display %ctx to %channel(%format, %g, %h, %i) newline = true radix = <hex> flags = [0, 0, 0, 0] : !simulation.bytes, !word, !word, !word
      // Equal X can disappear; clearing X must still commit.
      simulation.nba.enqueue %x to %q : (!word, !ref) -> ()
      simulation.nba.enqueue %zero to %q : (!word, !ref) -> ()
      simulation.suspend.delay %delay to ^third
    ^third:
      %j = simulation.ref.load %q : !ref -> !word
      %k = simulation.ref.load %mixed : !ref -> !word
      %l = simulation.ref.load %partial : !ref -> !word
      simulation.display %ctx to %channel(%format, %j, %k, %l) newline = true radix = <hex> flags = [0, 0, 0, 0] : !simulation.bytes, !word, !word, !word
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
