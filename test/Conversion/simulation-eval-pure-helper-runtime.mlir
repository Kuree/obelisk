// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-eval-pure-helper-runtime.test.

// A context-free value helper followed by a termination poll must not create
// a checkpoint per activation. Real finish/fatal effects still use cold routes.
// The predicate may duplicate this helper, but must not duplicate stateful ones.
// PLAN: llvm.func @publish.__obelisk_eval_body_0.__obelisk_checkpoint_path
// PLAN: llvm.func @__obelisk_eval_dispatch_v1
// The initial checker finishes through its ordinary timed continuation, not
// an eval-owner checkpoint. Neither clock activation may leave the evaluator.
!wide = !simulation.logic<65>
!wref = !simulation.ref<!wide>
!ref = !simulation.ref<i1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  simulation.design @pure_value_helper {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !wide design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "publish"
    simulation.code_unit.decl 4 in 0 always hierarchy "change"
    simulation.code_unit.decl 5 in 0 always hierarchy "posedge"
    simulation.code_unit.decl 6 in 0 initial hierarchy "check"
    simulation.code_unit.decl 7 in 0 function hierarchy "identity"
    simulation.code_unit.decl 8 in 0 function hierarchy "invert"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %data = simulation.context.storage %ctx[1] : !wref
      %change = simulation.context.storage %ctx[2] : !ref
      %edge = simulation.context.storage %ctx[3] : !ref
      %zero = arith.constant false
      %zeros = simulation.logic.constant 0 : i65, 0 : i65 : !wide
      simulation.ref.store %zero to %clk : i1, !ref
      simulation.ref.store %zeros to %data : !wide, !wref
      simulation.ref.store %zero to %change : i1, !ref
      simulation.ref.store %zero to %edge : i1, !ref
      %a = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %b = simulation.spawn @publish(%ctx, %clk, %data) : !simulation.context, !ref, !wref -> !simulation.process
      %c = simulation.spawn @change(%ctx, %data, %change) : !simulation.context, !wref, !ref -> !simulation.process
      %d = simulation.spawn @edge(%ctx, %data, %edge) : !simulation.context, !wref, !ref -> !simulation.process
      %e = simulation.spawn @check(%ctx, %data, %change, %edge) : !simulation.context, !wref, !ref, !ref -> !simulation.process
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
    simulation.func @publish(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^write {site = #schedule.continuation<id = 2>} : !ref
    ^write:
      %value = simulation.ref.load %clk : !ref -> i1
      %reported = simulation.call @identity(%ctx, %value) : (!simulation.context, i1) -> i1
      %requested = simulation.termination.requested %ctx
      cf.cond_br %requested, ^terminated, ^validate
    ^terminated:
      simulation.return
    ^validate:
      %bad = arith.xori %reported, %value : i1
      cf.cond_br %bad, ^fatal, ^choose
    ^fatal:
      %verbosity = arith.constant 0 : i32
      simulation.fatal %ctx, %verbosity
      simulation.return
    ^choose:
      cf.cond_br %reported, ^high, ^low
    ^high:
      %h = simulation.logic.constant 18446744073709551616 : i65, 0 : i65 : !wide
      simulation.ref.store %h to %data : !wide, !wref
      cf.br ^wait
    ^low:
      %l = simulation.logic.constant 18446744073709551615 : i65, 0 : i65 : !wide
      simulation.ref.store %l to %data : !wide, !wref
      cf.br ^wait
    }
    simulation.func @change(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %flag: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %data to ^changed {site = #schedule.continuation<id = 3>} : !wref
    ^changed:
      %one = arith.constant true
      simulation.ref.store %one to %flag : i1, !ref
      cf.br ^wait
    }
    simulation.func @edge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %flag: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.any %data edges [1] to ^edge {site = #schedule.continuation<id = 4>} : !wref
    ^edge:
      %one = arith.constant true
      simulation.ref.store %one to %flag : i1, !ref
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %change: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %edge: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %format = simulation.bytes.constant "%017h %b %b"
      %stdout = arith.constant 1 : i32
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^first {site = #schedule.continuation<id = 5>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^first:
      %v0 = simulation.ref.load %data : !wref -> !wide
      %c0 = simulation.ref.load %change : !ref -> i1
      %e0 = simulation.ref.load %edge : !ref -> i1
      simulation.display %ctx to %stdout(%format, %v0, %c0, %e0) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, !wide, i1, i1
      %two = simulation.time.constant 2
      simulation.suspend.delay %two to ^second {site = #schedule.continuation<id = 6>, timing = #schedule.timing_site<id = 2, kind = calendar>}
    ^second:
      %v1 = simulation.ref.load %data : !wref -> !wide
      %c1 = simulation.ref.load %change : !ref -> i1
      %e1 = simulation.ref.load %edge : !ref -> i1
      simulation.display %ctx to %stdout(%format, %v1, %c1, %e1) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, !wide, i1, i1
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
    simulation.func private @identity(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i1 {simulation.capture_kind = 2 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %inverted = simulation.call @invert(%ctx, %value) : (!simulation.context, i1) -> i1
      %result = simulation.call @invert(%ctx, %inverted) : (!simulation.context, i1) -> i1
      simulation.return %result : i1
    }
    simulation.func private @invert(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i1 {simulation.capture_kind = 2 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %one = arith.constant true
      %result = arith.xori %value, %one : i1
      simulation.return %result : i1
    }
  }
}
