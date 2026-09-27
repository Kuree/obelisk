// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph{vpi=off},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-shared-inactive-boundary-runtime.test.

// #0 is a local Inactive boundary. It must not commit an earlier NBA,
// replay the Active peer, or hand the complete plan to another scheduler.
// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 2 : i32
} {
  simulation.design @boundary {
    simulation.scope.decl 0 hierarchy "boundary"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "boundary.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "boundary.worker"
    simulation.code_unit.decl 3 in 0 initial hierarchy "boundary.peer"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design hierarchy "boundary.value"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.ref.store %zero to %ref : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %worker = simulation.spawn @worker(%ctx) : !simulation.context -> !simulation.process
      %peer = simulation.spawn @peer(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @worker(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.nba.enqueue %one to %ref : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      %zero = simulation.time.constant 0
      simulation.suspend.delay %zero to ^inactive {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^inactive:
      %current = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %value = simulation.ref.load %current : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %channel = arith.constant 1 : i32
      %message = simulation.bytes.constant "inactive value=%0d"
      simulation.display %ctx to %channel(%message, %value) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^later {site = #schedule.continuation<id = 2>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^later:
      %final = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %committed = simulation.ref.load %final : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %stdout = arith.constant 1 : i32
      %format = simulation.bytes.constant "later value=%0d"
      simulation.display %ctx to %stdout(%format, %committed) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      simulation.return
    }
    simulation.func @peer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %stdout = arith.constant 1 : i32
      %message = simulation.bytes.constant "active peer"
      simulation.display %ctx to %stdout(%message) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
