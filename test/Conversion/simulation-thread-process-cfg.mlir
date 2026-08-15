// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg)))' | FileCheck %s

module {
  obelisk_sim.design @thread_process_cfg {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "thread_process_cfg.process"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "thread_process_cfg.cyclic_resume"

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

    // A loop can enter with the original value, suspend with that value, and
    // later re-enter through a restored continuation argument. Downstream
    // uses must follow the restored lane instead of retaining the dominating
    // pre-suspension SSA definition.
    obelisk_sim.func @cyclic_resume(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %live = arith.addi %input, %input : i32
      cf.br ^loop(%live : i32)
    ^loop(%current: i32):
      %condition = arith.constant true
      cf.cond_br %condition, ^wait, ^use
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%live : i32)
    ^resume(%restored: i32):
      cf.br ^loop(%restored : i32)
    ^use:
      obelisk_sim.file.flush %ctx, %live :
          (!obelisk_sim.context, i32) -> ()
      cf.br ^loop(%current : i32)
    }
  }

  obelisk_sim.design @duplicate_successor {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "duplicate_successor.process"

    obelisk_sim.func @duplicate_successor_process(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %live = arith.addi %input, %input : i32
      %condition = arith.constant true
      cf.cond_br %condition, ^use, ^use
    ^use:
      obelisk_sim.file.flush %ctx, %live :
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

// CHECK-LABEL: obelisk_sim.func @cyclic_resume
// CHECK: cf.br ^[[LOOP:.*]](%[[LIVE:.*]] : i32)
// CHECK: ^[[LOOP]](%[[CURRENT:.*]]: i32):
// CHECK: cf.cond_br %{{.*}}, ^[[WAIT:.*]](%[[CURRENT]] : i32), ^[[USE:.*]](%[[CURRENT]] : i32)
// CHECK: ^[[WAIT]](%[[WAIT_VALUE:.*]]: i32):
// CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[RESUME:.*]](%[[WAIT_VALUE]] : i32)
// CHECK: ^[[RESUME]](%[[RESTORED:.*]]: i32):
// CHECK: cf.br ^[[LOOP]](%[[RESTORED]] : i32)
// CHECK: ^[[USE]](%[[USE_VALUE:.*]]: i32):
// CHECK: obelisk_sim.file.flush %{{.*}}, %[[USE_VALUE]]

// CHECK-LABEL: obelisk_sim.func @duplicate_successor_process
// CHECK: %[[DUP_LIVE:.*]] = arith.addi
// CHECK: cf.cond_br %{{.*}}, ^[[DUP_USE:.*]](%[[DUP_LIVE]] : i32), ^[[DUP_USE]](%[[DUP_LIVE]] : i32)
// CHECK: ^[[DUP_USE]](%[[DUP_THREADED:.*]]: i32):
// CHECK: obelisk_sim.file.flush %{{.*}}, %[[DUP_THREADED]]
