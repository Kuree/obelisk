// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-vpi-write-tier1-runtime.test.

// A writer arrives only AFTER 500 Tier-1 clock activations. Deposits must
// update the canonical planes; force must survive NBA writes; variable release
// retains the forced value until the next assignment. Depositing X invalidates
// two-state promotion, and a later known deposit permits recovery.
// PLAN-DAG: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// PLAN-DAG: llvm.func @__obelisk_eval_dispatch_v1
// Both long, writer-free intervals must use Tier 1. Runtime work stays
// bounded by the seven actual mutations, including recovery after release.
// The shared loop counts bootstrap, plan-node and barrier iterations as well
// as descriptor execution. The count remains independent of the long intervals.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @vpi_write {
    simulation.scope.decl 0 hierarchy "vpi_write"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "vpi_write.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "vpi_write.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "vpi_write.writer"
    simulation.code_unit.decl 4 in 0 initial hierarchy "vpi_write.check"
    simulation.code_unit.decl 5 in 0 always hierarchy "vpi_write.drive"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design hierarchy "vpi_write.clk"
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design hierarchy "vpi_write.q"
    simulation.net.decl 0 in 0 : !simulation.logic<32> design hierarchy "vpi_write.net"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<32> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %q = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %zero32 = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      simulation.ref.store %zero to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %zero32 to %q : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %initialDriver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<32>>
      simulation.driver.drive %initialDriver = %zero32 : !simulation.driver<!simulation.logic<32>>, !simulation.logic<32>
      %w = simulation.spawn @writer(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %t = simulation.spawn @check(%ctx) : !simulation.context -> !simulation.process
      %d = simulation.spawn @drive(%ctx, %q) : !simulation.context, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^write {site = #schedule.continuation<id = 2>} : !simulation.ref<!simulation.logic<1>>
    ^write:
      %q = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %old = simulation.ref.load %q : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %next = simulation.logic.binary add %old, %one : !simulation.logic<32>
      simulation.nba.enqueue %next to %q : (!simulation.logic<32>, !simulation.ref<!simulation.logic<32>>) -> ()
      cf.br ^wait
    }
    simulation.func @drive(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %q: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %q to ^drive {site = #schedule.continuation<id = 6>} : !simulation.ref<!simulation.logic<32>>
    ^drive:
      %value = simulation.ref.load %q : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<32>>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<32>>, !simulation.logic<32>
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i32
      %delay = simulation.time.constant 1000
      simulation.suspend.delay %delay to ^mutate(%zero : i32) {site = #schedule.continuation<id = 3>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^mutate(%phase: i32):
      %q = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %before = simulation.ref.load %q : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %ch = arith.constant 1 : i32
      %fmt = simulation.bytes.constant "before %0d %08h"
      simulation.display %ctx to %ch(%fmt, %phase, %before) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, i32, !simulation.logic<32>
      %after = simulation.ref.load %q : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<32>>
      %driven = simulation.net.read %net : !simulation.net<!simulation.logic<32>> -> !simulation.logic<32>
      %out = simulation.bytes.constant "after %0d %08h net=%08h"
      simulation.display %ctx to %ch(%out, %phase, %after, %driven) newline = true radix = <hex> flags = [0, 0, 0, 0] : !simulation.bytes, i32, !simulation.logic<32>, !simulation.logic<32>
      %one = arith.constant 1 : i32
      %next = arith.addi %phase, %one : i32
      %five = arith.constant 7 : i32
      %again = arith.cmpi ult, %next, %five : i32
      cf.cond_br %again, ^wait, ^settle
    ^wait:
      %step = simulation.time.constant 2
      simulation.suspend.delay %step to ^mutate(%next : i32) {site = #schedule.continuation<id = 4>, timing = #schedule.timing_site<id = 2, kind = calendar>}
    ^settle:
      %long = simulation.time.constant 1000
      simulation.suspend.delay %long to ^done {site = #schedule.continuation<id = 5>, timing = #schedule.timing_site<id = 3, kind = calendar>}
    ^done:
      %finalRef = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %final = simulation.ref.load %finalRef : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %channel = arith.constant 1 : i32
      %finalNet = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<32>>
      %finalDriven = simulation.net.read %finalNet : !simulation.net<!simulation.logic<32>> -> !simulation.logic<32>
      %format = simulation.bytes.constant "done %08h net=%08h"
      simulation.display %ctx to %channel(%format, %final, %finalDriven) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<32>, !simulation.logic<32>
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
