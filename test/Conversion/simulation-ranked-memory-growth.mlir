// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-replicate,convert-scf-to-cf,simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2> %t.trace
// RUN: FileCheck %s < %t.trace

// Over a hundred CFG fragments (32 replicated diamonds) must not duplicate
// this owner's whole summary. Its 32 independent state bits and one trigger
// have 33 read ranges and 32 write ranges, independent of the number of
// fragment identities.
// CHECK: obelisk ranked memory: body=chain.__obelisk_eval_body_0 reads=33 writes=32{{$}}

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32, native.debug_timing} {
  simulation.design @chain {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design {test.replicate = 32 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.code_unit.decl 2 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 2 : i64} {
      %p = simulation.spawn @chain(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "chain"
    // Storage 0 is the trigger; storage 1 ... 32 are the state bits.
    simulation.func @chain(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, test.activation_max_value_nodes = 512 : i64} {
      %root = simulation.context.storage %ctx[1] {test.replicate = 32 : i64, test.replicate_stride = {id = 1 : i64}} : !simulation.ref<i1>
      %trigger = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %zero = arith.constant false
      %one = arith.constant true
      cf.br ^body
    ^body:
      %flag = simulation.ref.load %trigger : !simulation.ref<i1> -> i1
      scf.execute_region {
        %old = simulation.ref.load %root : !simulation.ref<i1> -> i1
        %changed = arith.xori %old, %one : i1
        scf.if %flag {
          simulation.ref.store %changed to %root : i1, !simulation.ref<i1>
        } else {
          simulation.ref.store %old to %root : i1, !simulation.ref<i1>
        }
        scf.yield
      } {test.replicate = 32 : i64}
      simulation.suspend.change %trigger to ^body : !simulation.ref<i1>
    }
  }
}
