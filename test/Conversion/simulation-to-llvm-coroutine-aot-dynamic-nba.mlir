// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// A dynamic, potentially overhanging packed NBA to a planned static root must
// remain on the generated AOT path. Its overlap, mask, and source shift are
// scalar operations independent of the selected width: no scheduler call,
// allocation, loop, or width-proportional CFG is introduced in the top tier.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 2 : i32
} {
  obelisk_sim.design @aot_dynamic_nba {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "aot_dynamic_nba.root"
    obelisk_sim.code_unit.decl 2 in 0 initial
        hierarchy "aot_dynamic_nba.process"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<64> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %process = obelisk_sim.spawn @process(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %destination = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %low_storage = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %low = obelisk_sim.ref.load %low_storage :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %slice = obelisk_sim.ref.dyn_extract %destination from %low :
          (!obelisk_sim.ref<!obelisk_sim.logic<8>>,
           !obelisk_sim.logic<64>) -> !obelisk_sim.ref<!obelisk_sim.logic<12>>
      %value = obelisk_sim.logic.constant 2748 : i12, 0 : i12 :
          !obelisk_sim.logic<12>
      obelisk_sim.nba.enqueue %value to %slice :
          (!obelisk_sim.logic<12>,
           !obelisk_sim.ref<!obelisk_sim.logic<12>>) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK-DAG: llvm.mlir.global internal @__obelisk_aot_nba_accumulator_0
// CHECK-DAG: llvm.mlir.global internal @__obelisk_aot_nba_dirty_roots_v1
// CHECK-LABEL: llvm.func @process(
// CHECK: llvm.icmp "sgt"
// CHECK: llvm.icmp "slt"
// CHECK: llvm.shl
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_accumulator_0
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// CHECK: llvm.store
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_packed_slice_nba
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_nba
// CHECK-NOT: llvm.call @malloc
// CHECK: llvm.return
