// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.mlir

// Runtime behavior is checked in ../Runtime/simulation-native-delay-memory.test.

// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
// Ten million nonzero suspensions must retain O(live actors), not O(timesteps),
// generic calendar entries even when only the native calendar is consulted.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 2 : i32
} {
  simulation.design @delay_memory {
    simulation.scope.decl 0 hierarchy "delay_memory"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "delay_memory.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "delay_memory.loop"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %p = simulation.spawn @loop(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @loop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %count = arith.constant 10000000 : i64
      cf.br ^check(%count : i64)
    ^check(%left: i64):
      %zero = arith.constant 0 : i64
      %more = arith.cmpi ugt, %left, %zero : i64
      cf.cond_br %more, ^wait, ^done
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%left : i64)
    ^resume(%remaining: i64):
      %one = arith.constant 1 : i64
      %next = arith.subi %remaining, %one : i64
      cf.br ^check(%next : i64)
    ^done:
      %text = simulation.bytes.constant "done"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%text) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
