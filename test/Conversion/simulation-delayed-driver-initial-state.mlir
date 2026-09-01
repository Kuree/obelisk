// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | %python %S/Inputs/dump-bytecode-instructions.py --state | FileCheck %s --check-prefix=BYTECODE

// IEEE 1800-2017 4.9.1, 6.7.1, and 28.16: a delayed continuous driver
// starts at x until its time-zero evaluation reaches the output. An ordinary
// driver still starts at z.
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

// The net and both drivers are unknown. Their value bits are respectively Z,
// Z, and X, so only the first two bits are set in the value plane.
// NATIVE: llvm.mlir.global internal @__obelisk_state_unknown("\07\00\00\00\00\00\00\00\00")
// NATIVE: llvm.mlir.global internal @__obelisk_state_value("\03\00\00\00\00\00\00\00\00")

// Bit 14 records initial X on the delayed driver's descriptor. The ordinary
// driver retains the legacy/default-Z flags.
// BYTECODE: state 2: kind=driver flags=953 value=1 target=0 width=1
// BYTECODE: state 3: kind=driver flags=17337 value=2 target=0 width=1
