// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.mlir

// Runtime behavior is checked in ../Runtime/simulation-native-delay-memory.test.

// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
// Ten million nonzero suspensions must retain O(live actors), not O(timesteps),
// generic calendar entries even when only the native calendar is consulted.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 2 : i32
} {
  obelisk_sim.design @delay_memory {
    obelisk_sim.scope.decl 0 hierarchy "delay_memory"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "delay_memory.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "delay_memory.loop"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %p = obelisk_sim.spawn @loop(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @loop(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %count = arith.constant 10000000 : i64
      cf.br ^check(%count : i64)
    ^check(%left: i64):
      %zero = arith.constant 0 : i64
      %more = arith.cmpi ugt, %left, %zero : i64
      cf.cond_br %more, ^wait, ^done
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%left : i64)
    ^resume(%remaining: i64):
      %one = arith.constant 1 : i64
      %next = arith.subi %remaining, %one : i64
      cf.br ^check(%next : i64)
    ^done:
      %text = obelisk_sim.bytes.constant "done"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%text) newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
