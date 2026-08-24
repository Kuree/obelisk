// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py --state \
// RUN:   | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @trireg_charge_strength {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "drive"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      charge_strength = 1 : i32,
      resolution_kind = 9 : i32
    }
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design {
      charge_strength = 4 : i32,
      resolution_kind = 9 : i32
    }
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
    obelisk_sim.driver.decl 0 in 0 drives 1 :
        !obelisk_sim.logic<1> design {resolution_kind = 9 : i32}
    obelisk_sim.func @drive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %z = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %z :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
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
