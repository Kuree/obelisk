// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py --state \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @strength_banks {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      resolution_kind = 2 : i32
    }
    simulation.driver.decl 0 in 0 drives 0 :
        !simulation.logic<1> design {
      driven_low = 0 : i64,
      driven_width = 1 : i64,
      strength0 = 6 : i32,
      strength1 = 0 : i32,
      simulation.strength_group = 7 : i64,
      simulation.strength_bank = 0 : i32
    }
    simulation.driver.decl 1 in 0 drives 0 :
        !simulation.logic<1> design {
      driven_low = 0 : i64,
      driven_width = 1 : i64,
      strength0 = 0 : i32,
      strength1 = 6 : i32,
      simulation.strength_group = 7 : i64,
      simulation.strength_bank = 1 : i32
    }
    simulation.code_unit.decl 1 in 0 initial hierarchy "process" debug "process"
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %low = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %z = simulation.logic.constant true, true : !simulation.logic<1>
      simulation.driver.drive %low = %z {
        schedule.defer_net_resolution
      } : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %high = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %high = %one :
          !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.return
    }
  }
}

// CHECK: state 1: kind=net flags=5
// CHECK: state 2: kind=driver flags=189 {{.*}}strength0=6 strength1=0
// CHECK: state 3: kind=driver flags=2957 {{.*}}strength0=0 strength1=6
// CHECK: opcode=28 flags=4
// CHECK: opcode=28 flags=0
