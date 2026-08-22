// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py --state \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @strength_banks {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      resolution_kind = 2 : i32
    }
    obelisk_sim.driver.decl 0 in 0 drives 0 :
        !obelisk_sim.logic<1> design {
      driven_low = 0 : i64,
      driven_width = 1 : i64,
      strength0 = 6 : i32,
      strength1 = 0 : i32,
      obelisk_sim.strength_group = 7 : i64,
      obelisk_sim.strength_bank = 0 : i32
    }
    obelisk_sim.driver.decl 1 in 0 drives 0 :
        !obelisk_sim.logic<1> design {
      driven_low = 0 : i64,
      driven_width = 1 : i64,
      strength0 = 0 : i32,
      strength1 = 6 : i32,
      obelisk_sim.strength_group = 7 : i64,
      obelisk_sim.strength_bank = 1 : i32
    }
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "process" debug "process"
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %low = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %z = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %low = %z {
        obelisk_sim.defer_net_resolution
      } : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %high = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %high = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// CHECK: state 1: kind=net flags=5
// CHECK: state 2: kind=driver flags=189 {{.*}}strength0=6 strength1=0
// CHECK: state 3: kind=driver flags=2957 {{.*}}strength0=0 strength1=6
// CHECK: opcode=28 flags=4
// CHECK: opcode=28 flags=0
