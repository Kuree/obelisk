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
  simulation.design @user_net_state {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : f64 design {
      simulation.user_defined_net
    }
    simulation.driver.decl 0 in 0 drives 0 : f64 design
    simulation.code_unit.decl 1 in 0 function hierarchy "resolve"

    simulation.func @resolve(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<f64>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<f64>
      %value = simulation.driver.read %driver :
          !simulation.driver<f64> -> f64
      simulation.net.write %net = %value : !simulation.net<f64>, f64
      simulation.return
    }
  }
}

// NATIVE-LABEL: llvm.func @resolve
// NATIVE: llvm.mlir.addressof @__obelisk_state_value
// NATIVE: llvm.load
// NATIVE: llvm.store
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_real_transition
// NATIVE-NOT: simulation.driver.read
// NATIVE-NOT: simulation.net.write

// LoadState and StoreState are append-only bytecode opcodes 27 and 28.
// BYTECODE: opcode=27 flags=0
// BYTECODE: opcode=28 flags=0
