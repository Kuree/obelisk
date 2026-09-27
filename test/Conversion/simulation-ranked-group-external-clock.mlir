// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-ranked-group-external-clock.test.

// The NBA counter feeds a two-actor native group before VPI observes count.
// The model has no internal clock process. Every edge is a VPI deposit.
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: schedule.eval.ranked_members
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @external_clock {
    obelisk_sim.scope.decl 0 hierarchy "external_clock"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "external_clock.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "external_clock.counter"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "external_clock.clk"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design hierarchy "external_clock.accumulator"
    obelisk_sim.code_unit.decl 3 in 0 always_comb hierarchy "external_clock.relay1"
    obelisk_sim.code_unit.decl 4 in 0 always_comb hierarchy "external_clock.relay2"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<32> design hierarchy "external_clock.count"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %count = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %zero32 = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      obelisk_sim.ref.store %zero to %clk : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero32 to %count : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %middle = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %visible = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %relay1 = obelisk_sim.spawn @relay1(%ctx, %count, %middle) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<32>>, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      %relay2 = obelisk_sim.spawn @relay2(%ctx, %middle, %visible) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<32>>, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      %process = obelisk_sim.spawn @counter(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
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
    obelisk_sim.func @relay1(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %output: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^step
    ^wait:
      obelisk_sim.suspend.change %input to ^step {site = #schedule.continuation<id = 2>} : !obelisk_sim.ref<!obelisk_sim.logic<32>>
    ^step:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.ref.store %value to %output : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      cf.br ^wait
    }
    obelisk_sim.func @relay2(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %output: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^step
    ^wait:
      obelisk_sim.suspend.change %input to ^step {site = #schedule.continuation<id = 3>} : !obelisk_sim.ref<!obelisk_sim.logic<32>>
    ^step:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.ref.store %value to %output : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      cf.br ^wait
    }
  }
}
