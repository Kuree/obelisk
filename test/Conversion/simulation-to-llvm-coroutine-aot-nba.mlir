// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --implicit-check-not=obelisk_sim.nba.enqueue

// Exercise AOT NBA planning and materialization from hand-authored simulation
// IR. Driver option parsing is deliberately outside this pass test.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 2 : i32
} {
  obelisk_sim.design @aot_nba {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "aot_nba.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "aot_nba.process"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %process = obelisk_sim.spawn @process(%ctx, %storage) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %destination: !obelisk_sim.ref<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = obelisk_sim.logic.constant 42 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %value to %destination :
          (!obelisk_sim.logic<8>,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_aot_nba_roots_v1
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_aot_nba_sites_v1
// CHECK-DAG: llvm.func @__obelisk_aot_static_nba_commit_v1
// CHECK-DAG: llvm.call @obelisk_rt_v1_static_nba_claim
// CHECK-DAG: llvm.call @obelisk_rt_v1_static_nba_commit_roots
// CHECK-DAG: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
