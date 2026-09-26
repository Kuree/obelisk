// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-ordered-eval-nba-runtime.test.

// The writer runs twice before the NBA barrier: src=1 stages one, then a
// different actor resets src and stages zero at the SAME semantic site.
// The final value is zero, but a posedge observer must still run (LRM 4.6).
// PLAN: llvm.mlir.global internal @__obelisk_eval_ordered_nba_queue_v1
// PLAN: llvm.func @__obelisk_eval_dispatch_v1

!bit = !obelisk_sim.logic<1>
!ref = !obelisk_sim.ref<!bit>
!wide = !obelisk_sim.logic<65>
!wref = !obelisk_sim.ref<!wide>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @ordered {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !bit design
    obelisk_sim.storage.decl 1 in 0 : !bit design
    obelisk_sim.storage.decl 2 in 0 : !bit design
    obelisk_sim.storage.decl 3 in 0 : !wide design
    obelisk_sim.storage.decl 4 in 0 : !bit design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "ordered.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "ordered.clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "ordered.forward"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "ordered.writer"
    obelisk_sim.code_unit.decl 5 in 0 always hierarchy "ordered.reset"
    obelisk_sim.code_unit.decl 6 in 0 always hierarchy "ordered.edge"
    obelisk_sim.code_unit.decl 7 in 0 initial hierarchy "ordered.check"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !ref
      %src = obelisk_sim.context.storage %ctx[1] : !ref
      %kick = obelisk_sim.context.storage %ctx[2] : !ref
      %data = obelisk_sim.context.storage %ctx[3] : !wref
      %seen = obelisk_sim.context.storage %ctx[4] : !ref
      %zero = obelisk_sim.logic.constant false, false : !bit
      %zeros = obelisk_sim.logic.constant 0 : i65, 0 : i65 : !wide
      obelisk_sim.ref.store %zero to %clk : !bit, !ref
      obelisk_sim.ref.store %zero to %src : !bit, !ref
      obelisk_sim.ref.store %zero to %kick : !bit, !ref
      obelisk_sim.ref.store %zeros to %data : !wide, !wref
      obelisk_sim.ref.store %zero to %seen : !bit, !ref
      %a = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !ref -> !obelisk_sim.process
      %b = obelisk_sim.spawn @forward(%ctx, %clk, %src) : !obelisk_sim.context, !ref, !ref -> !obelisk_sim.process
      %c = obelisk_sim.spawn @writer(%ctx, %src, %kick, %data) : !obelisk_sim.context, !ref, !ref, !wref -> !obelisk_sim.process
      %d = obelisk_sim.spawn @reset(%ctx, %kick, %src) : !obelisk_sim.context, !ref, !ref -> !obelisk_sim.process
      %e = obelisk_sim.spawn @edge(%ctx, %data, %seen) : !obelisk_sim.context, !wref, !ref -> !obelisk_sim.process
      %f = obelisk_sim.spawn @check(%ctx, %data, %seen) : !obelisk_sim.context, !wref, !ref -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^toggle {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !ref -> !bit
      %new = obelisk_sim.logic.unary bit_not %old : (!bit) -> !bit
      obelisk_sim.ref.store %new to %clk : !bit, !ref
      cf.br ^wait
    }
    obelisk_sim.func @forward(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %src: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clk to ^copy {site = #obelisk_sim.continuation<id = 2>} : !ref
    ^copy:
      %value = obelisk_sim.ref.load %clk : !ref -> !bit
      obelisk_sim.ref.store %value to %src : !bit, !ref
      cf.br ^wait
    }
    obelisk_sim.func @writer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %src: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %kick: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %src to ^write {site = #obelisk_sim.continuation<id = 3>} : !ref
    ^write:
      %value = obelisk_sim.ref.load %src : !ref -> !bit
      %target = obelisk_sim.ref.extract %data from 0 : !wref -> !ref
      obelisk_sim.nba.enqueue %value to %target : (!bit, !ref) -> ()
      obelisk_sim.ref.store %value to %kick : !bit, !ref
      cf.br ^wait
    }
    obelisk_sim.func @reset(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %kick: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %src: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.any %kick edges [1] to ^reset {site = #obelisk_sim.continuation<id = 4>} : !ref
    ^reset:
      %zero = obelisk_sim.logic.constant false, false : !bit
      obelisk_sim.ref.store %zero to %src : !bit, !ref
      cf.br ^wait
    }
    obelisk_sim.func @edge(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %seen: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.any %data edges [1] to ^edge {site = #obelisk_sim.continuation<id = 5>} : !wref
    ^edge:
      %one = obelisk_sim.logic.constant true, false : !bit
      obelisk_sim.ref.store %one to %seen : !bit, !ref
      cf.br ^wait
    }
    obelisk_sim.func @check(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %seen: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay to ^check {site = #obelisk_sim.continuation<id = 6>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^check:
      %value = obelisk_sim.ref.load %data : !wref -> !wide
      %edge = obelisk_sim.ref.load %seen : !ref -> !bit
      %format = obelisk_sim.bytes.constant "%017h %b"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %value, %edge) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !wide, !bit
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
