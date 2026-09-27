// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-vpi-periodic-timer-runtime.test.

// Three foreign observations interrupt long intervals of periodic Tier-1 work.
// A callback deadline is a boundary, not a persistent live-observation lease.
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @external_clock {
    obelisk_sim.scope.decl 0 hierarchy "external_clock"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "external_clock.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "external_clock.counter"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "external_clock.clock"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "external_clock.clk"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design hierarchy "external_clock.count"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %count = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %zero32 = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      obelisk_sim.ref.store %zero to %clk : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero32 to %count : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %process = obelisk_sim.spawn @counter(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %osc = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @counter(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^step {site = #schedule.continuation<id = 1>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^step:
      %count = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %old = obelisk_sim.ref.load %count : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %one = obelisk_sim.logic.constant 1 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %next = obelisk_sim.logic.binary add %old, %one : !obelisk_sim.logic<32>
      obelisk_sim.nba.enqueue %next to %count : (!obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      cf.br ^wait
    }
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 2>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clk : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
  }
}
