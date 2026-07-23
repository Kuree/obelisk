// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off})' | FileCheck %s --check-prefix=O0
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-captures,obelisk_sim.func(canonicalize,cse),symbol-dce,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off})' | FileCheck %s --check-prefix=O1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // O0: obelisk.bytecode.image
  // O1: obelisk.bytecode.image
  obelisk_sim.design @lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"

    // O0-LABEL: obelisk_sim.func private @process(
    // O0-SAME: !obelisk_sim.context
    // O0-SAME: }, %arg1: i32
    // O1-LABEL: obelisk_sim.func private @process(
    // O1-SAME: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes
    obelisk_sim.func private @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }

    // O0-LABEL: obelisk_sim.func @root(
    // O0: obelisk_sim.spawn @process(%arg0, %c0_i32)
    // O1-LABEL: obelisk_sim.func @root(
    // O1: obelisk_sim.spawn @process(%arg0)
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %zero = arith.constant 0 : i32
      %child = obelisk_sim.spawn @process(%ctx, %zero)
          : !obelisk_sim.context, i32 -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}
