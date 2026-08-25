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
    obelisk_sim.code_unit.decl 7 in 0 function hierarchy "driver_lowering.drive_delayed_net"
    obelisk_sim.code_unit.decl 8 in 0 function hierarchy "driver_lowering.drive_wand"
    obelisk_sim.code_unit.decl 9 in 0 function hierarchy "driver_lowering.drive_tri0"
    obelisk_sim.code_unit.decl 10 in 0 function hierarchy "driver_lowering.drive_trireg"
    obelisk_sim.code_unit.decl 11 in 0 function hierarchy "driver_lowering.drive_delayed_trireg"
    obelisk_sim.code_unit.decl 12 in 0 function hierarchy "driver_lowering.drive_inertial_strength_pair"
    obelisk_sim.code_unit.decl 13 in 0 function hierarchy "driver_lowering.drive_inertial_real"
    obelisk_sim.code_unit.decl 14 in 0 function hierarchy "driver_lowering.drive_inertial_path"
    obelisk_sim.code_unit.decl 15 in 0 function hierarchy "driver_lowering.case_difference"
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
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, 11, 13>
    }
    obelisk_sim.driver.decl 4 in 0 drives 3 :
        !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 4 in 0 : !obelisk_sim.logic<1> design {
      resolution_kind = 3 : i32
    }
    obelisk_sim.driver.decl 5 in 0 drives 4 :
        !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 6 in 0 drives 4 :
        !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 5 in 0 : !obelisk_sim.logic<1> design {
      resolution_kind = 5 : i32
    }
    obelisk_sim.driver.decl 7 in 0 drives 5 :
        !obelisk_sim.logic<1> design {resolution_kind = 5 : i32}
    obelisk_sim.net.decl 6 in 0 : !obelisk_sim.logic<1> design {
      resolution_kind = 9 : i32
    }
    obelisk_sim.driver.decl 8 in 0 drives 6 :
        !obelisk_sim.logic<1> design {resolution_kind = 9 : i32}
    obelisk_sim.net.decl 7 in 0 : !obelisk_sim.logic<1> design {
      propagation_delays = array<i64: 7, 11, -1>,
      resolution_kind = 9 : i32
    }
    obelisk_sim.driver.decl 9 in 0 drives 7 :
        !obelisk_sim.logic<1> design {resolution_kind = 9 : i32}
    obelisk_sim.net.decl 8 in 0 : f64 design {
      obelisk_sim.user_defined_net
    }
    obelisk_sim.driver.decl 10 in 0 drives 8 : f64 design

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

    obelisk_sim.func @drive_inertial_path(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 14 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %value = obelisk_sim.logic.constant 1 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      %active = arith.constant 3 : i2
      %rise_mask = arith.constant 1 : i2
      %fall_mask = arith.constant 2 : i2
      %turnoff_mask = arith.constant 3 : i2
      %rise = obelisk_sim.time.constant 7
      %fall = obelisk_sim.time.constant 11
      %turnoff = obelisk_sim.time.constant 13
      obelisk_sim.driver.drive_inertial_path %driver = %value
          active %active masks [%rise_mask, %fall_mask, %turnoff_mask]
          after [%rise, %fall, %turnoff] site 14 : 2 group 0 of 1
          {defer_resolution = true} :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>, i2
      obelisk_sim.return
    }

    obelisk_sim.func @case_difference(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %lhs: !obelisk_sim.logic<2> {obelisk_sim.capture_kind = 2 : i32},
        %rhs: !obelisk_sim.logic<2> {obelisk_sim.capture_kind = 2 : i32})
        -> i2 attributes {entry_kind = 8 : i32, code_unit_id = 15 : i64} {
      %mask = obelisk_sim.logic.case_difference_mask %lhs, %rhs :
          (!obelisk_sim.logic<2>, !obelisk_sim.logic<2>) -> i2
      obelisk_sim.return %mask : i2
    }

    obelisk_sim.func @drive_inertial_strength_pair(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %low = obelisk_sim.context.driver %ctx[2] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %high = obelisk_sim.context.driver %ctx[3] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 :
          !obelisk_sim.logic<1>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %rise = obelisk_sim.time.constant 7
      %fall = obelisk_sim.time.constant 11
      %turnoff = obelisk_sim.time.constant 13
      obelisk_sim.driver.drive_inertial_strength_pair
          %low = %z, %high = %one transition %one
          after[%rise, %fall, %turnoff] site 12 : 0 :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_delayed_net(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %driver = obelisk_sim.context.driver %ctx[4] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive_delayed_net %driver = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_wand(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %driver = obelisk_sim.context.driver %ctx[5] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %zero :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_tri0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %driver = obelisk_sim.context.driver %ctx[7] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_trireg(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %driver = obelisk_sim.context.driver %ctx[8] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %z :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @drive_delayed_trireg(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %driver = obelisk_sim.context.driver %ctx[9] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive_delayed_net %driver = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    // IEEE 1800-2017 6.6.7 and 10.3.3: one delay applies to the atomic real
    // UDNT contribution, whose publication wakes the generated resolver.
    obelisk_sim.func @drive_inertial_real(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: f64 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 13 : i64} {
      %driver = obelisk_sim.context.driver %ctx[10] :
          !obelisk_sim.driver<f64>
      %delay = obelisk_sim.time.constant 7
      obelisk_sim.driver.drive_inertial %driver = %value
          after[%delay, %delay, %delay] site 13 : 0 vector = true
          {defer_resolution = true, obelisk_sim.user_net_raw_drive} :
          !obelisk_sim.driver<f64>, f64
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
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: obelisk_sim.driver.drive

// CHECK-LABEL: llvm.func @drive_inertial
// CHECK: llvm.icmp
// CHECK: llvm.select
// CHECK: llvm.call @obelisk_rt_v1_scheduler_inertial_driver
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: obelisk_sim.driver.drive_inertial

// CHECK-LABEL: llvm.func @drive_inertial_path
// CHECK: llvm.call @obelisk_rt_v1_scheduler_inertial_path_driver
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: obelisk_sim.driver.drive_inertial_path

// CHECK-LABEL: llvm.func @case_difference
// CHECK: llvm.xor
// CHECK: llvm.xor
// CHECK: llvm.or
// CHECK-NOT: obelisk_sim.logic.case_difference_mask

// The two polarity banks of a delayed three-state primitive lower through one
// atomic scheduler ABI call. Its separate logical transition planes select
// the LRM rise/fall/turn-off/x delay.
// CHECK-LABEL: llvm.func @drive_inertial_strength_pair
// CHECK: llvm.call @obelisk_rt_v1_scheduler_inertial_driver_strength_pair
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: obelisk_sim.driver.drive_inertial_strength_pair

// The driver contribution is stored immediately, then resolution schedules
// the delayed visible-net update through the runtime.
// CHECK-LABEL: llvm.func @drive_delayed_net
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: llvm.call @obelisk_rt_v1_scheduler_resolve_drivers
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: obelisk_sim.driver.drive_delayed_net

// Native wired resolution passes the effective kind to the shared exact
// strength resolver. 3 is the canonical wand/triand kind.
// CHECK-LABEL: llvm.func @drive_wand
// CHECK: %[[WAND:.*]] = llvm.mlir.constant(3 : i32) : i32
// CHECK: llvm.call @obelisk_rt_v1_strength_resolve_kind({{.*}}, {{.*}}, %[[WAND]])
// CHECK-NOT: obelisk_sim.driver.drive

// IEEE 1800-2017 6.6.5 gives tri0 an implicit pull0 contribution. Native
// resolution seeds Figure 28-2 position -5 (bit index 2) and passes kind 5.
// CHECK-LABEL: llvm.func @drive_tri0
// CHECK: %[[PULL0:.*]] = llvm.mlir.constant(4 : i16) : i16
// CHECK: %[[TRI0:.*]] = llvm.mlir.constant(5 : i32) : i32
// CHECK: llvm.call @obelisk_rt_v1_strength_resolve_kind(%[[PULL0]], {{.*}}, %[[TRI0]])
// CHECK-NOT: obelisk_sim.driver.drive

// IEEE 1800-2017 6.6.4: native trireg resolution uses ordinary wire
// strengths while driven and selects the old resolved planes when all
// drivers are z.
// CHECK-LABEL: llvm.func @drive_trireg
// CHECK: %[[TRIREG:.*]] = llvm.mlir.constant(9 : i32) : i32
// CHECK: llvm.call @obelisk_rt_v1_strength_resolve_kind({{.*}}, {{.*}}, %[[TRIREG]])
// CHECK: llvm.select
// CHECK: llvm.select
// CHECK-NOT: obelisk_sim.driver.drive

// IEEE 1800-2017 28.16.2 delayed trireg publication uses the same runtime
// scheduler in native execution as in bytecode execution.
// CHECK-LABEL: llvm.func @drive_delayed_trireg
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: llvm.call @obelisk_rt_v1_scheduler_resolve_drivers
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: obelisk_sim.driver.drive_delayed_net

// Raw delayed real contributions use vector-delay, deferred-resolution,
// raw-publication, and f64 flags: 1 | 2 | 4 | 16 = 23.
// CHECK-LABEL: llvm.func @drive_inertial_real
// CHECK: %[[REAL_FLAGS:.*]] = llvm.mlir.constant(23 : i32) : i32
// CHECK: llvm.call @obelisk_rt_v1_scheduler_inertial_driver
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: obelisk_sim.driver.drive_inertial

// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010236 inputs=8 outputs=0 flags=0
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010237 inputs=10 outputs=0 flags=0
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010241 inputs=14 outputs=0 flags=0
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010240 inputs=2 outputs=1 flags=0
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010236 inputs={{\[[0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+\]}} outputs=[]
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010237 inputs={{\[[0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+\]}} outputs=[]
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010241 inputs={{\[[0-9, ]+\]}} outputs=[]
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010240 inputs={{\[[0-9]+, [0-9]+\]}} outputs={{\[[0-9]+\]}}
