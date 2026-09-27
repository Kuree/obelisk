// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// Native lowering must resolve only the static topology components touched by
// a partial packed driver update. In particular, the 512-bit declarations in
// this test must not make a scalar primitive drive emit 512 copies of the
// strength resolver and transition CFG.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @static_driver_components {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "drive_scalar"
    simulation.code_unit.decl 2 in 0 function hierarchy "drive_multi"
    simulation.code_unit.decl 3 in 0 function hierarchy "drive_alias"
    simulation.code_unit.decl 4 in 0 function hierarchy "drive_dynamic"
    simulation.code_unit.decl 5 in 0 function hierarchy "drive_declared_range"

    simulation.net.decl 0 in 0 : !simulation.logic<512> design
    simulation.driver.decl 0 in 0 drives 0 :
        !simulation.logic<512> design
    simulation.net.decl 1 in 0 : !simulation.logic<512> design
    simulation.net.connect.decl 0 in 0 0[301] to 1[7] width 1 reversed = false

    simulation.net.decl 2 in 0 : !simulation.logic<8> design
    simulation.driver.decl 1 in 0 drives 2 :
        !simulation.logic<8> design

    simulation.net.decl 3 in 0 : !simulation.logic<512> design
    simulation.driver.decl 2 in 0 drives 3 :
        !simulation.logic<512> design {
      driven_low = 400 : i64, driven_width = 5 : i64
    }

    simulation.func @drive_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<512>>
      %bit = simulation.driver.extract %root from 100 :
          !simulation.driver<!simulation.logic<512>> ->
          !simulation.driver<!simulation.logic<1>>
      %one = simulation.logic.constant 1 : i1, 0 : i1 :
          !simulation.logic<1>
      simulation.driver.drive %bit = %one :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }

    simulation.func @drive_multi(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<512>>
      %part = simulation.driver.extract %root from 200 :
          !simulation.driver<!simulation.logic<512>> ->
          !simulation.driver<!simulation.logic<7>>
      %ones = simulation.logic.constant 127 : i7, 0 : i7 :
          !simulation.logic<7>
      simulation.driver.drive %part = %ones :
          !simulation.driver<!simulation.logic<7>>,
          !simulation.logic<7>
      simulation.return
    }

    simulation.func @drive_alias(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<512>>
      %bit = simulation.driver.extract %root from 301 :
          !simulation.driver<!simulation.logic<512>> ->
          !simulation.driver<!simulation.logic<1>>
      %zero = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      simulation.driver.drive %bit = %zero :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }

    simulation.func @drive_dynamic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<8>>
      %bit = simulation.driver.dyn_extract %root from %index :
          (!simulation.driver<!simulation.logic<8>>, i64) ->
          !simulation.driver<!simulation.logic<1>>
      %one = simulation.logic.constant 1 : i1, 0 : i1 :
          !simulation.logic<1>
      simulation.driver.drive %bit = %one :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }

    // Even a full-width write to a driver declared for one packed range can
    // only affect that declared range on its destination net.
    simulation.func @drive_declared_range(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %driver = simulation.context.driver %ctx[2] :
          !simulation.driver<!simulation.logic<512>>
      %ones = simulation.logic.constant -1 : i512, 0 : i512 :
          !simulation.logic<512>
      simulation.driver.drive %driver = %ones :
          !simulation.driver<!simulation.logic<512>>,
          !simulation.logic<512>
      simulation.return
    }

  }
}

// CHECK-LABEL: llvm.func @drive_scalar
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK: llvm.return

// CHECK-LABEL: llvm.func @drive_multi
// CHECK-COUNT-7: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK: llvm.return

// CHECK-LABEL: llvm.func @drive_alias
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK: llvm.return

// A dynamic view cannot be proven to touch one bit. The exact conservative
// fallback therefore resolves all eight possible destination bits.
// CHECK-LABEL: llvm.func @drive_dynamic
// CHECK: llvm.call @obelisk_rt_v1_native_handle_offset
// CHECK-COUNT-8: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK: llvm.return

// CHECK-LABEL: llvm.func @drive_declared_range
// CHECK-COUNT-5: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK: llvm.return

// CHECK-NOT: simulation.driver.drive
