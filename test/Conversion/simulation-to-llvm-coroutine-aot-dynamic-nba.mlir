// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba))' \
// RUN:   -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// Dynamic reads stay direct. The two NBA sites target different roots, so
// their update events use the ordered scheduler until a generated owner can
// replay both roots in source order.
// The second RUN invokes only the conversion pass so this test directly locks
// down that pass's generated hot path.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 2 : i32
} {
  simulation.design @aot_dynamic_nba {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "aot_dynamic_nba.root"
    simulation.code_unit.decl 2 in 0 initial
        hierarchy "aot_dynamic_nba.process"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<64> design
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %process = simulation.spawn @process(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %destination = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<8>>
      %low_storage = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<64>>
      %low = simulation.ref.load %low_storage :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %read_slice = simulation.ref.dyn_extract %low_storage from %low :
          (!simulation.ref<!simulation.logic<64>>,
           !simulation.logic<64>) -> !simulation.ref<!simulation.logic<32>>
      %read_value = simulation.ref.load %read_slice :
          !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %wide_destination = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<64>>
      %write_slice = simulation.ref.dyn_extract %wide_destination from %low :
          (!simulation.ref<!simulation.logic<64>>,
           !simulation.logic<64>) -> !simulation.ref<!simulation.logic<32>>
      simulation.nba.enqueue %read_value to %write_slice :
          (!simulation.logic<32>,
           !simulation.ref<!simulation.logic<32>>) -> ()
      %slice = simulation.ref.dyn_extract %destination from %low :
          (!simulation.ref<!simulation.logic<8>>,
           !simulation.logic<64>) -> !simulation.ref<!simulation.logic<12>>
      %value = simulation.logic.constant 2748 : i12, 0 : i12 :
          !simulation.logic<12>
      simulation.nba.enqueue %value to %slice :
          (!simulation.logic<12>,
           !simulation.ref<!simulation.logic<12>>) -> ()
      simulation.return
    }
  }
}

// CHECK-DAG: llvm.mlir.global internal @__obelisk_aot_nba_accumulator_0
// CHECK-DAG: llvm.mlir.global internal @__obelisk_aot_nba_dirty_roots_v1
// CHECK-LABEL: llvm.func @process(
// A fixed packed root uses a bounds-checked direct load. The invalid-handle
// branch preserves zero/X fallback for an unknown or out-of-range selection.
// CHECK: llvm.cond_br
// CHECK: llvm.mlir.addressof @__obelisk_state_value
// CHECK: llvm.load
// CHECK: llvm.mlir.addressof @__obelisk_state_unknown
// CHECK: llvm.load
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_scheduler_packed_slice_nba
// CHECK-NOT: llvm.call @malloc
// CHECK: llvm.return
