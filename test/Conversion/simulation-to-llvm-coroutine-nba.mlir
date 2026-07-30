// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @nba_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "nba_lowering.enqueue"

    obelisk_sim.func @enqueue(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %destination: !obelisk_sim.ref<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %value = obelisk_sim.logic.constant 42 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %value to %destination :
          (!obelisk_sim.logic<8>,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.nba.enqueue %value to %destination after %delay :
          (!obelisk_sim.logic<8>,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>,
           !obelisk_sim.time) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @enqueue
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_scheduler_nba
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: obelisk_sim.nba.enqueue
