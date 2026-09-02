// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | %python %S/Inputs/dump-bytecode-instructions.py --state | FileCheck %s --check-prefix=BYTECODE

// IEEE 1800-2017 4.9.1, 6.7.1, and 10.3.3: a continuous assignment is
// evaluated at time zero, but a delayed update does not reach its net until
// the selected propagation delay elapses. Both drivers therefore start at z.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @delayed_driver_initial_state {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "top.delayed"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @delayed(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %driver = obelisk_sim.context.driver %ctx[1] :
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
  }
}

// The net and both drivers are four-state. All three start at Z, so every
// corresponding value-plane bit is set.
// NATIVE: llvm.mlir.global internal @__obelisk_state_unknown("\01\01\01\00\00\00\00\00\00\00\00")
// NATIVE: llvm.mlir.global internal @__obelisk_state_value("\01\01\01\00\00\00\00\00\00\00\00")

// Neither descriptor carries the explicit initial-X bit.
// BYTECODE: state 2: kind=driver flags=953 value=8 target=0 width=1
// BYTECODE: state 3: kind=driver flags=953 value=16 target=0 width=1
