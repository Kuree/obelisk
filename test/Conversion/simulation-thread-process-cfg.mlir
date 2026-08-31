// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg)))' | FileCheck %s

module {
  obelisk_sim.design @thread_process_cfg {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "thread_process_cfg.process"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "thread_process_cfg.cyclic_resume"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "thread_process_cfg.side_resume"
    obelisk_sim.code_unit.decl 5 in 0 initial hierarchy "thread_process_cfg.control_side_resume"
    obelisk_sim.code_unit.decl 6 in 0 initial hierarchy "thread_process_cfg.constant_dag"
    obelisk_sim.code_unit.decl 7 in 0 initial hierarchy "thread_process_cfg.body_defined_resume"

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

    // A restored lane can rejoin a loop through a side block rather than
    // branching directly to its header. The original definition dominates
    // the exit in the source CFG, but not the coroutine's hidden resume path.
    obelisk_sim.func @side_resume(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 1 : i32} {
      %live = arith.addi %input, %input : i32
      cf.br ^loop
    ^loop:
      %condition = arith.constant true
      cf.cond_br %condition, ^wait, ^use
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%live : i32)
    ^resume(%restored: i32):
      cf.br ^side
    ^side:
      cf.br ^loop
    ^use:
      obelisk_sim.file.flush %ctx, %live :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.return
    }

    // The same reconvergence inside a synchronous named-block body must not
    // trust source-CFG dominance from the boundary entry. A later suspension
    // restores both the live value and the control activation on a path that
    // bypasses that entry.
    obelisk_sim.func @control_side_resume(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 5 : i64, entry_kind = 1 : i32} {
      %live = arith.addi %input, %input : i32
      %initial_delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %initial_delay to ^start(%live : i32)
    ^start(%initial: i32):
      %activation = obelisk_sim.control.enter 1
      obelisk_sim.control.boundary %activation resume ^exit body ^body
    ^exit:
      obelisk_sim.return
    ^body:
      cf.br ^loop
    ^loop:
      %condition = arith.constant true
      cf.cond_br %condition, ^wait, ^use
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(
          %initial, %activation : i32, !obelisk_sim.control)
    ^resume(%restored: i32, %restored_activation: !obelisk_sim.control):
      cf.br ^side
    ^side:
      cf.br ^loop
    ^use:
      obelisk_sim.file.flush %ctx, %initial :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.control.leave %activation
      cf.br ^exit
    }

    // A pure value derived only from constants is cheaper and safer to
    // recreate after a suspension than to reserve a canonical-frame lane and
    // thread it through every loop edge.
    obelisk_sim.func @constant_dag(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 6 : i64, entry_kind = 1 : i32} {
      %two = arith.constant 2 : i32
      %three = arith.constant 3 : i32
      %derived = arith.addi %two, %three : i32
      cf.br ^loop
    ^loop:
      %condition = arith.constant true
      cf.cond_br %condition, ^wait, ^use
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%derived : i32)
    ^resume(%restored: i32):
      cf.br ^loop
    ^use:
      obelisk_sim.file.flush %ctx, %derived :
          (!obelisk_sim.context, i32) -> ()
      cf.br ^loop
    }

    // A suspension-live root can be defined in a loop preheader rather than
    // the entry block. Recursive reconstruction must stop at that definition
    // for outgoing edges; the root need not exist on the initial path into
    // the preheader itself.
    obelisk_sim.func @body_defined_resume(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 7 : i64, entry_kind = 1 : i32} {
      cf.br ^make_value
    ^make_value:
      %live = arith.addi %input, %input : i32
      cf.br ^loop
    ^loop:
      %condition = arith.constant true
      cf.cond_br %condition, ^wait, ^use
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%live : i32)
    ^resume(%restored: i32):
      cf.br ^side
    ^side:
      cf.br ^loop
    ^use:
      obelisk_sim.file.flush %ctx, %live :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.return
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

// CHECK-LABEL: obelisk_sim.func @side_resume
// CHECK: cf.br ^[[SIDE_LOOP:.*]](%[[SIDE_LIVE:.*]] : i32)
// CHECK: ^[[SIDE_LOOP]](%[[SIDE_CURRENT:.*]]: i32):
// CHECK: cf.cond_br %{{.*}}, ^[[SIDE_WAIT:.*]](%[[SIDE_CURRENT]] : i32), ^[[SIDE_USE:.*]](%[[SIDE_CURRENT]] : i32)
// CHECK: ^[[SIDE_WAIT]](%[[SIDE_WAIT_VALUE:.*]]: i32):
// CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[SIDE_RESUME:.*]](%[[SIDE_WAIT_VALUE]] : i32)
// CHECK: ^[[SIDE_RESUME]](%[[SIDE_RESTORED:.*]]: i32):
// CHECK: cf.br ^[[SIDE:.*]](%[[SIDE_RESTORED]] : i32)
// CHECK: ^[[SIDE]](%[[SIDE_VALUE:.*]]: i32):
// CHECK: cf.br ^[[SIDE_LOOP]](%[[SIDE_VALUE]] : i32)
// CHECK: ^[[SIDE_USE]](%[[SIDE_USE_VALUE:.*]]: i32):
// CHECK: obelisk_sim.file.flush %{{.*}}, %[[SIDE_USE_VALUE]]

// CHECK-LABEL: obelisk_sim.func @control_side_resume
// CHECK: obelisk_sim.control.boundary %[[CONTROL_ACTIVATION:.*]] resume ^[[CONTROL_EXIT:.*]] body ^[[CONTROL_BODY:.*]]
// CHECK: ^[[CONTROL_BODY]]:
// CHECK: cf.br ^[[CONTROL_LOOP:.*]](%{{.*}}, %[[CONTROL_ACTIVATION]] : i32, !obelisk_sim.control)
// CHECK: ^[[CONTROL_LOOP]](%[[CONTROL_VALUE:.*]]: i32, %[[CONTROL_CURRENT:.*]]: !obelisk_sim.control):
// CHECK: cf.cond_br %{{.*}}, ^[[CONTROL_WAIT:.*]](%[[CONTROL_VALUE]], %[[CONTROL_CURRENT]] : i32, !obelisk_sim.control), ^[[CONTROL_USE:.*]](%[[CONTROL_VALUE]], %[[CONTROL_CURRENT]] : i32, !obelisk_sim.control)
// CHECK: ^[[CONTROL_WAIT]](%[[CONTROL_WAIT_VALUE:.*]]: i32, %[[CONTROL_WAIT_ACTIVATION:.*]]: !obelisk_sim.control):
// CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[CONTROL_RESUME:.*]](%[[CONTROL_WAIT_VALUE]], %[[CONTROL_WAIT_ACTIVATION]] : i32, !obelisk_sim.control)
// CHECK: ^[[CONTROL_RESUME]](%[[CONTROL_RESTORED:.*]]: i32, %[[CONTROL_RESTORED_ACTIVATION:.*]]: !obelisk_sim.control):
// CHECK: cf.br ^[[CONTROL_SIDE:.*]](%[[CONTROL_RESTORED]], %[[CONTROL_RESTORED_ACTIVATION]] : i32, !obelisk_sim.control)
// CHECK: ^[[CONTROL_SIDE]](%[[CONTROL_SIDE_VALUE:.*]]: i32, %[[CONTROL_SIDE_ACTIVATION:.*]]: !obelisk_sim.control):
// CHECK: cf.br ^[[CONTROL_LOOP]](%[[CONTROL_SIDE_VALUE]], %[[CONTROL_SIDE_ACTIVATION]] : i32, !obelisk_sim.control)
// CHECK: ^[[CONTROL_USE]](%[[CONTROL_USE_VALUE:.*]]: i32, %[[CONTROL_USE_ACTIVATION:.*]]: !obelisk_sim.control):
// CHECK: obelisk_sim.file.flush %{{.*}}, %[[CONTROL_USE_VALUE]]
// CHECK: obelisk_sim.control.leave %[[CONTROL_USE_ACTIVATION]]

// CHECK-LABEL: obelisk_sim.func @constant_dag
// CHECK: %[[ORIGINAL_TWO:.*]] = arith.constant 2 : i32
// CHECK-NEXT: %[[ORIGINAL_THREE:.*]] = arith.constant 3 : i32
// CHECK-NEXT: arith.addi %[[ORIGINAL_TWO]], %[[ORIGINAL_THREE]] : i32
// CHECK: %[[WAIT_TWO:.*]] = arith.constant 2 : i32
// CHECK-NEXT: %[[WAIT_THREE:.*]] = arith.constant 3 : i32
// CHECK-NEXT: %[[WAIT_DERIVED:.*]] = arith.addi %[[WAIT_TWO]], %[[WAIT_THREE]] : i32
// CHECK: obelisk_sim.suspend.delay %{{.*}} to ^{{.*}}(%[[WAIT_DERIVED]] : i32)
// CHECK: %[[USE_TWO:.*]] = arith.constant 2 : i32
// CHECK-NEXT: %[[USE_THREE:.*]] = arith.constant 3 : i32
// CHECK-NEXT: %[[USE_DERIVED:.*]] = arith.addi %[[USE_TWO]], %[[USE_THREE]] : i32
// CHECK-NEXT: obelisk_sim.file.flush %{{.*}}, %[[USE_DERIVED]]

// CHECK-LABEL: obelisk_sim.func @body_defined_resume
// CHECK: ^[[BODY_DEF:.*]]:
// CHECK-NEXT: %[[BODY_LIVE:.*]] = arith.addi
// CHECK-NEXT: cf.br ^[[BODY_LOOP:.*]](%[[BODY_LIVE]] : i32)
// CHECK: ^[[BODY_LOOP]](%[[BODY_CURRENT:.*]]: i32):
// CHECK: cf.cond_br %{{.*}}, ^[[BODY_WAIT:.*]](%[[BODY_CURRENT]] : i32), ^[[BODY_USE:.*]](%[[BODY_CURRENT]] : i32)
// CHECK: ^[[BODY_WAIT]](%[[BODY_WAIT_VALUE:.*]]: i32):
// CHECK: obelisk_sim.suspend.delay %{{.*}} to ^[[BODY_RESUME:.*]](%[[BODY_WAIT_VALUE]] : i32)
// CHECK: ^[[BODY_RESUME]](%[[BODY_RESTORED:.*]]: i32):
// CHECK: cf.br ^[[BODY_SIDE:.*]](%[[BODY_RESTORED]] : i32)
// CHECK: ^[[BODY_SIDE]](%[[BODY_SIDE_VALUE:.*]]: i32):
// CHECK: cf.br ^[[BODY_LOOP]](%[[BODY_SIDE_VALUE]] : i32)
// CHECK: ^[[BODY_USE]](%[[BODY_USE_VALUE:.*]]: i32):
// CHECK: obelisk_sim.file.flush %{{.*}}, %[[BODY_USE_VALUE]]

// CHECK-LABEL: obelisk_sim.func @duplicate_successor_process
// CHECK: %[[DUP_LIVE:.*]] = arith.addi
// CHECK: cf.cond_br %{{.*}}, ^[[DUP_USE:.*]](%[[DUP_LIVE]] : i32), ^[[DUP_USE]](%[[DUP_LIVE]] : i32)
// CHECK: ^[[DUP_USE]](%[[DUP_THREADED:.*]]: i32):
// CHECK: obelisk_sim.file.flush %{{.*}}, %[[DUP_THREADED]]
