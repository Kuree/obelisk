// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// Native lowering must resolve only the static topology components touched by
// a partial packed driver update. In particular, the 512-bit declarations in
// this test must not make a scalar primitive drive emit 512 copies of the
// strength resolver and transition CFG.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @static_driver_components {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "drive_scalar"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "drive_multi"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "drive_alias"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "drive_dynamic"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "drive_declared_range"

    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<512> design
    obelisk_sim.driver.decl 0 in 0 drives 0 :
        !obelisk_sim.logic<512> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<512> design
    obelisk_sim.net.connect.decl 0 in 0 0[301] to 1[7] width 1 reversed = false

    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 1 in 0 drives 2 :
        !obelisk_sim.logic<8> design

    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<512> design
    obelisk_sim.driver.decl 2 in 0 drives 3 :
        !obelisk_sim.logic<512> design {
      driven_low = 400 : i64, driven_width = 5 : i64
    }

    obelisk_sim.func @drive_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<512>>
      %bit = obelisk_sim.driver.extract %root from 100 :
          !obelisk_sim.driver<!obelisk_sim.logic<512>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_multi(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<512>>
      %part = obelisk_sim.driver.extract %root from 200 :
          !obelisk_sim.driver<!obelisk_sim.logic<512>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<7>>
      %ones = obelisk_sim.logic.constant 127 : i7, 0 : i7 :
          !obelisk_sim.logic<7>
      obelisk_sim.driver.drive %part = %ones :
          !obelisk_sim.driver<!obelisk_sim.logic<7>>,
          !obelisk_sim.logic<7>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_alias(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<512>>
      %bit = obelisk_sim.driver.extract %root from 301 :
          !obelisk_sim.driver<!obelisk_sim.logic<512>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit = %zero :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_dynamic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %index: i64 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<8>>
      %bit = obelisk_sim.driver.dyn_extract %root from %index :
          (!obelisk_sim.driver<!obelisk_sim.logic<8>>, i64) ->
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    // Even a full-width write to a driver declared for one packed range can
    // only affect that declared range on its destination net.
    obelisk_sim.func @drive_declared_range(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %driver = obelisk_sim.context.driver %ctx[2] :
          !obelisk_sim.driver<!obelisk_sim.logic<512>>
      %ones = obelisk_sim.logic.constant -1 : i512, 0 : i512 :
          !obelisk_sim.logic<512>
      obelisk_sim.driver.drive %driver = %ones :
          !obelisk_sim.driver<!obelisk_sim.logic<512>>,
          !obelisk_sim.logic<512>
      obelisk_sim.return
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

// CHECK-NOT: obelisk_sim.driver.drive
