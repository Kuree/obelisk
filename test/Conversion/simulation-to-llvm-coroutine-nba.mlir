// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode -o /dev/null

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @nba_lowering {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "nba_lowering.enqueue"

    simulation.func @enqueue(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %value = simulation.logic.constant 42 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %value to %destination :
          (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      %delay = simulation.time.constant 5
      simulation.nba.enqueue %value to %destination after %delay :
          (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>,
           !simulation.time) -> ()
      simulation.nba.enqueue %value to %destination
          {clocking_output = 42 : i64} :
          (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @enqueue
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_scheduler_nba
// CHECK: llvm.call @obelisk_rt_v1_scheduler_clocking_nba
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: simulation.nba.enqueue
