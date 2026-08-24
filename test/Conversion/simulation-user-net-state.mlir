// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

// IEEE 1800-2017 6.6.7: a generated user-net resolver reads the raw atomic
// driver contribution and publishes the resolved atomic net value.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @user_net_state {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : f64 design {
      obelisk_sim.user_defined_net
    }
    obelisk_sim.driver.decl 0 in 0 drives 0 : f64 design
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "resolve"

    obelisk_sim.func @resolve(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<f64>
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<f64>
      %value = obelisk_sim.driver.read %driver :
          !obelisk_sim.driver<f64> -> f64
      obelisk_sim.net.write %net = %value : !obelisk_sim.net<f64>, f64
      obelisk_sim.return
    }
  }
}

// NATIVE-LABEL: llvm.func @resolve
// NATIVE: llvm.mlir.addressof @__obelisk_state_value
// NATIVE: llvm.load
// NATIVE: llvm.store
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_real_transition
// NATIVE-NOT: obelisk_sim.driver.read
// NATIVE-NOT: obelisk_sim.net.write

// LoadState and StoreState are append-only bytecode opcodes 27 and 28.
// BYTECODE: opcode=27 flags=0
// BYTECODE: opcode=28 flags=0
