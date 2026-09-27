// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py --state \
// RUN:   | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @trireg_charge_strength {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "drive"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      charge_strength = 1 : i32,
      resolution_kind = 9 : i32
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {
      charge_strength = 4 : i32,
      resolution_kind = 9 : i32
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
    simulation.driver.decl 0 in 0 drives 1 :
        !simulation.logic<1> design {resolution_kind = 9 : i32}
    simulation.func @drive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %z = simulation.logic.constant true, true : !simulation.logic<1>
      simulation.driver.drive %driver = %z :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }
  }
}

// IEEE 1800-2017 28.16 and 28.16.2: descriptor bits 7-8 preserve small and
// large charge strength independently from the trireg resolution kind.
// BYTECODE: state 0: kind=net flags=195
// BYTECODE: state 1: kind=net flags=451
// BYTECODE: state 2: kind=driver flags=9147

// The native tier resolves the active drivers and both retained charges with
// the same strength-range runtime primitive before atomic publication.
// NATIVE-COUNT-3: llvm.call @obelisk_rt_v1_strength_resolve_kind
