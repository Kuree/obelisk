// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | %python %S/Inputs/dump-bytecode-instructions.py --state | FileCheck %s --check-prefix=BYTECODE

// IEEE 1800-2017 4.9.1, 6.7.1, 10.3.3, and 30.5: a net and an ordinary
// continuous-assignment contribution start at Z while the time-zero
// evaluation schedules its first propagated value. Module paths instead use
// X as the initial transition source for path-delay selection.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @delayed_driver_initial_state {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "top.delayed"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "top.path"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "top.strength"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "top.path_strength"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 1 : !simulation.logic<1> design {
      strength1 = 0 : i32,
      simulation.strength_group = 1 : i64,
      simulation.strength_bank = 0 : i32
    }
    simulation.driver.decl 3 in 0 drives 1 : !simulation.logic<1> design {
      strength0 = 0 : i32,
      simulation.strength_group = 1 : i64,
      simulation.strength_bank = 1 : i32
    }
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.driver.decl 4 in 0 drives 2 : !simulation.logic<1> design {
      strength1 = 0 : i32,
      simulation.strength_group = 2 : i64,
      simulation.strength_bank = 0 : i32
    }
    simulation.driver.decl 5 in 0 drives 2 : !simulation.logic<1> design {
      strength0 = 0 : i32,
      simulation.strength_group = 2 : i64,
      simulation.strength_bank = 1 : i32
    }

    simulation.func @delayed(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %value = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      %delay = simulation.time.constant 5
      simulation.driver.drive_inertial %driver = %value
          after[%delay, %delay, %delay] site 1 : 0 vector = false :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }

    simulation.func @path(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      %driver = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %value = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      %active = arith.constant 1 : i1
      %delay = simulation.time.constant 5
      simulation.driver.drive_inertial_path %driver = %value active %active
          masks[%active, %active, %active] after[%delay, %delay, %delay]
          site 2 : 0 group 0 of 1 :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, i1
      simulation.return
    }

    simulation.func @strength(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      %low = simulation.context.driver %ctx[2] :
          !simulation.driver<!simulation.logic<1>>
      %high = simulation.context.driver %ctx[3] :
          !simulation.driver<!simulation.logic<1>>
      %z = simulation.logic.constant 1 : i1, 1 : i1 :
          !simulation.logic<1>
      %one = simulation.logic.constant 1 : i1, 0 : i1 :
          !simulation.logic<1>
      %delay = simulation.time.constant 5
      simulation.driver.drive_inertial_strength_pair
          %low = %z, %high = %one transition %one
          after[%delay, %delay, %delay] site 3 : 0 :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.logic<1>
      simulation.return
    }

    simulation.func @path_strength(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %low = simulation.context.driver %ctx[4] :
          !simulation.driver<!simulation.logic<1>>
      %high = simulation.context.driver %ctx[5] :
          !simulation.driver<!simulation.logic<1>>
      %z = simulation.logic.constant 1 : i1, 1 : i1 :
          !simulation.logic<1>
      %one = simulation.logic.constant 1 : i1, 0 : i1 :
          !simulation.logic<1>
      %active = arith.constant 1 : i1
      %delay = simulation.time.constant 5
      simulation.driver.drive_inertial_path_strength_pair
          %low = %z, %high = %one transition %one active %active
          masks[%active, %active, %active] after[%delay, %delay, %delay]
          site 4 : 0 group 0 of 1 :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.logic<1>, i1
      simulation.return
    }
  }
}

// Each net and ordinary delayed driver starts at Z. Module-path delayed
// drivers start at X. Every four-state slot therefore has its unknown plane
// set, while the value plane distinguishes Z from X.
// NATIVE: llvm.mlir.global internal constant @__obelisk_state_initializers_v1(dense<[0, 1, 1, 1, 8, 1, 1, 1, 16, 1, 0, 1, 24, 1, 1, 1, 32, 1, 1, 1, 40, 1, 1, 1, 48, 1, 1, 1, 56, 1, 0, 1, 64, 1, 0, 1]> : tensor<36xi64>)
// NATIVE: llvm.mlir.global internal @__obelisk_state_unknown()
// NATIVE: llvm.mlir.global internal @__obelisk_state_value()

// Bit 14 records initial X only on module-path driver descriptors, including
// both strength banks.
// BYTECODE: state 7: kind=driver flags=953 value=8 target=0 width=1
// BYTECODE: state 8: kind=driver flags=17337 value=16 target=0 width=1
// BYTECODE: state 9: kind=driver flags=185 value=32 target=24 width=1
// BYTECODE: state 10: kind=driver flags=2953 value=40 target=24 width=1
// BYTECODE: state 11: kind=driver flags=16569 value=56 target=48 width=1
// BYTECODE: state 12: kind=driver flags=19337 value=64 target=48 width=1
