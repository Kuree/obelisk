// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | %python %S/Inputs/dump-bytecode-instructions.py --state | FileCheck %s --check-prefix=BYTECODE

// IEEE 1800-2017 4.9.1, 10.3.3, 28.16, and 30.5: time-zero evaluation of an
// inertial driver schedules its first propagated value after the selected
// delay. Its contribution is X before that update, including module paths.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @delayed_driver_initial_state {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "top.delayed"
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "top.path"
    obelisk_sim.code_unit.decl 3 in 0 continuous hierarchy "top.strength"
    obelisk_sim.code_unit.decl 4 in 0 continuous hierarchy "top.path_strength"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 1 : !obelisk_sim.logic<1> design {
      strength1 = 0 : i32,
      obelisk_sim.strength_group = 1 : i64,
      obelisk_sim.strength_bank = 0 : i32
    }
    obelisk_sim.driver.decl 3 in 0 drives 1 : !obelisk_sim.logic<1> design {
      strength0 = 0 : i32,
      obelisk_sim.strength_group = 1 : i64,
      obelisk_sim.strength_bank = 1 : i32
    }
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 4 in 0 drives 2 : !obelisk_sim.logic<1> design {
      strength1 = 0 : i32,
      obelisk_sim.strength_group = 2 : i64,
      obelisk_sim.strength_bank = 0 : i32
    }
    obelisk_sim.driver.decl 5 in 0 drives 2 : !obelisk_sim.logic<1> design {
      strength0 = 0 : i32,
      obelisk_sim.strength_group = 2 : i64,
      obelisk_sim.strength_bank = 1 : i32
    }

    obelisk_sim.func @delayed(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial %driver = %value
          after[%delay, %delay, %delay] site 1 : 0 vector = false :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @path(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      %driver = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %active = arith.constant 1 : i1
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial_path %driver = %value active %active
          masks[%active, %active, %active] after[%delay, %delay, %delay]
          site 2 : 0 group 0 of 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1
      obelisk_sim.return
    }

    obelisk_sim.func @strength(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      %low = obelisk_sim.context.driver %ctx[2] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %high = obelisk_sim.context.driver %ctx[3] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 :
          !obelisk_sim.logic<1>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial_strength_pair
          %low = %z, %high = %one transition %one
          after[%delay, %delay, %delay] site 3 : 0 :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @path_strength(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %low = obelisk_sim.context.driver %ctx[4] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %high = obelisk_sim.context.driver %ctx[5] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 :
          !obelisk_sim.logic<1>
      %one = obelisk_sim.logic.constant 1 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %active = arith.constant 1 : i1
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial_path_strength_pair
          %low = %z, %high = %one transition %one active %active
          masks[%active, %active, %active] after[%delay, %delay, %delay]
          site 4 : 0 group 0 of 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, i1
      obelisk_sim.return
    }
  }
}

// Each net starts at Z and every delayed driver starts at X. Thus every
// four-state slot has its unknown plane set, while only the nets have their
// value plane set.
// NATIVE: llvm.mlir.global internal @__obelisk_state_unknown("\01\01\01\01\01\01\01\01\01\00\00\00\00\00\00\00\00")
// NATIVE: llvm.mlir.global internal @__obelisk_state_value("\01\00\00\01\00\00\01\00\00\00\00\00\00\00\00\00\00")

// Bit 14 records initial X on every delayed-driver descriptor, including both
// strength banks.
// BYTECODE: state 7: kind=driver flags=17337 value=8 target=0 width=1
// BYTECODE: state 8: kind=driver flags=17337 value=16 target=0 width=1
// BYTECODE: state 9: kind=driver flags=16569 value=32 target=24 width=1
// BYTECODE: state 10: kind=driver flags=19337 value=40 target=24 width=1
// BYTECODE: state 11: kind=driver flags=16569 value=56 target=48 width=1
// BYTECODE: state 12: kind=driver flags=19337 value=64 target=48 width=1
