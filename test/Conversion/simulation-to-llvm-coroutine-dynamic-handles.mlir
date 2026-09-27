// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --implicit-check-not=simulation.ref.dyn_extract

// Dynamic packed views are valid when any selected bit overlaps the source.
// This is the conversion contract that lets the runtime preserve in-range bits
// of a partially out-of-range direct or nonblocking assignment.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @dynamic_handles {
    simulation.code_unit.decl 1 in 0 function
        hierarchy "test.dynamic_handles.partial.1"
    simulation.scope.decl 0

    simulation.func @partial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<4>>
            {simulation.capture_kind = 1 : i32},
        %low: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %view = simulation.ref.dyn_extract %input from %low :
          (!simulation.ref<!simulation.logic<4>>, i64) ->
          !simulation.ref<!simulation.logic<2>>
      %value = simulation.ref.load %view :
          !simulation.ref<!simulation.logic<2>> ->
          !simulation.logic<2>
      simulation.ref.store %value to %view :
          !simulation.logic<2>, !simulation.ref<!simulation.logic<2>>
      simulation.nba.enqueue %value to %view :
          (!simulation.logic<2>, !simulation.ref<!simulation.logic<2>>) -> ()
      simulation.return
    }
  }
}

// A two-bit result may start at -1 or 3 and still overlap a four-bit input.
// Starts below -1 or above 3 become the invalid handle instead.
// CHECK-LABEL: llvm.func @partial(
// CHECK-DAG: %[[MIN:.*]] = llvm.mlir.constant(-1 : i64) : i64
// CHECK-DAG: %[[MAX:.*]] = llvm.mlir.constant(3 : i64) : i64
// CHECK: %[[OVERLAPS_LOW:.*]] = llvm.icmp "sge" %{{.*}}, %[[MIN]]
// CHECK: %[[OVERLAPS_HIGH:.*]] = llvm.icmp "sle" %{{.*}}, %[[MAX]]
// CHECK: llvm.call @obelisk_rt_v1_native_handle_offset
// CHECK: llvm.select
// CHECK: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: llvm.call @obelisk_rt_v1_native_state_store_plane
// The scheduler receives the exact four-bit view bound separately from the
// two-bit payload. This preserves IEEE 1800-2017 11.5.1 clipping without
// allowing an overhanging write to spill into an adjacent aggregate element.
// CHECK: llvm.call @obelisk_rt_v1_scheduler_packed_slice_nba
