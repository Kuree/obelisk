// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @driver_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "driver_lowering.drive"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "driver_lowering.drive_changed"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "driver_lowering.drive_wide"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "driver_lowering.drive_nba"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "driver_lowering.drive_strength"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "driver_lowering.drive_inertial"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.driver.decl 0 in 0 drives 0 :
        !obelisk_sim.logic<2> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<65536> design
    obelisk_sim.driver.decl 1 in 0 drives 1 :
        !obelisk_sim.logic<65536> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 2 :
        !obelisk_sim.logic<1> design {
      strength1 = 0 : i32,
      obelisk_sim.strength_group = 1 : i64,
      obelisk_sim.strength_bank = 0 : i32
    }
    obelisk_sim.driver.decl 3 in 0 drives 2 :
        !obelisk_sim.logic<1> design {
      strength0 = 0 : i32,
      obelisk_sim.strength_group = 1 : i64,
      obelisk_sim.strength_bank = 1 : i32
    }

    obelisk_sim.func @drive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %value = obelisk_sim.logic.constant 1 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_changed(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %value = obelisk_sim.logic.constant 1 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      %changed = obelisk_sim.driver.drive_changed %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>
      obelisk_sim.return %changed : i1
    }

    obelisk_sim.func @drive_wide(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<65536>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %driver = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<65536>>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65536>>,
          !obelisk_sim.logic<65536>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_nba(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %value = obelisk_sim.logic.constant 2 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      obelisk_sim.nba.enqueue %value to %driver
          {clocking_output = 42 : i64} :
          (!obelisk_sim.logic<2>,
           !obelisk_sim.driver<!obelisk_sim.logic<2>>) -> ()
      obelisk_sim.return
    }

    obelisk_sim.func @drive_strength(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %driver = obelisk_sim.context.driver %ctx[2] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %z {
        obelisk_sim.defer_net_resolution
      } : !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      %driver_high = obelisk_sim.context.driver %ctx[3] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver_high = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_inertial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %index: !obelisk_sim.logic<3> {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %selected = obelisk_sim.driver.dyn_extract %driver from %index :
          (!obelisk_sim.driver<!obelisk_sim.logic<2>>,
           !obelisk_sim.logic<3>) ->
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %value = obelisk_sim.logic.constant 1 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      %rise = obelisk_sim.time.constant 7
      %fall = obelisk_sim.time.constant 11
      %turnoff = obelisk_sim.time.constant 13
      obelisk_sim.driver.drive_inertial %selected = %value
          after[%rise, %fall, %turnoff] site 6 : 4 vector = true
          {defer_resolution = true} :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @drive
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: llvm.mlir.addressof @__obelisk_state_value
// CHECK: llvm.load
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @__obelisk_state_unknown
// CHECK: llvm.store
// CHECK: llvm.cond_br
// CHECK: %[[ROOT:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK: %[[WIDTH:.*]] = llvm.mlir.constant(2 : i64) : i64
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_scheduler_static_transition({{.*}}, %[[ROOT]], %[[OFFSET]], %[[WIDTH]], {{.*}})
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// CHECK-NOT: obelisk_sim.driver.drive

// CHECK-LABEL: llvm.func @drive_changed
// CHECK: llvm.or
// CHECK: llvm.return %{{.*}} : i1
// CHECK-NOT: obelisk_sim.driver.drive_changed

// CHECK-LABEL: llvm.func @drive_wide
// CHECK: llvm.store %{{.*}}, %{{.*}} : i65536, !llvm.ptr
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// CHECK-NOT: obelisk_sim.driver.drive

// CHECK-LABEL: llvm.func @drive_nba
// CHECK: llvm.call @obelisk_rt_v1_scheduler_clocking_driver_nba
// CHECK-NOT: obelisk_sim.nba.enqueue

// A highz1 driver cannot use the single-driver memcpy shortcut. Native
// lowering resolves the same Figure 28-2 range representation as bytecode.
// CHECK-LABEL: llvm.func @drive_strength
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_strength_resolve
// CHECK-NOT: obelisk_sim.driver.drive

// CHECK-LABEL: llvm.func @drive_inertial
// CHECK: llvm.icmp
// CHECK: llvm.select
// CHECK: llvm.call @obelisk_rt_v1_scheduler_inertial_driver
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: obelisk_sim.driver.drive_inertial

// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010236 inputs=8 outputs=0 flags=0
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010236 inputs={{\[[0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+\]}} outputs=[]
