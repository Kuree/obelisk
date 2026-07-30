// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg)))' | FileCheck %s

module {
  obelisk_sim.design @thread_process_cfg {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "thread_process_cfg.process"

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %live = arith.addi %input, %input : i32
      %one = arith.constant 1 : i32
      cf.br ^use(%one : i32)
    ^use(%constant: i32):
      %sum = arith.addi %live, %constant : i32
      obelisk_sim.file.flush %ctx, %sum :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @process
// CHECK: %[[LIVE:.*]] = arith.addi
// CHECK: cf.br ^[[USE:.*]](%[[LIVE]] : i32)
// CHECK: ^[[USE]](%[[THREADED:.*]]: i32):
// CHECK-NEXT: %[[ONE:.*]] = arith.constant 1 : i32
// CHECK-NEXT: %[[SUM:.*]] = arith.addi %[[THREADED]], %[[ONE]]
// CHECK: obelisk_sim.file.flush %{{.*}}, %[[SUM]]
