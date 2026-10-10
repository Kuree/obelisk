// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph),test-obelisk-activation-phases)' -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2023 4.5: phase p of a time slot runs after p - 1 NBA regions.
// Each block prints `{time-zero phases|later-slot phases}`. The root
// initializer starts processes at time zero in phase 1; a positive delay
// moves to phase 1 of a later slot; an NBA made in phase p wakes its waiters
// in phase p + 1 and a blocking update in the same phase; a zero delay stays
// in its slot in any phase; a task without suspensions runs and returns in
// its caller's phases. NBA feedback widens to `32+` while keeping its first
// phase, and a process that never delays never leaves time zero.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module {
  simulation.design @phases {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.storage.decl 4 in 0 : !bit design
    simulation.storage.decl 5 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "flop"
    simulation.code_unit.decl 4 in 0 always hierarchy "echo"
    simulation.code_unit.decl 5 in 0 always hierarchy "pingpong"
    simulation.code_unit.decl 6 in 0 initial hierarchy "zero"
    simulation.code_unit.decl 7 in 0 initial hierarchy "caller"
    simulation.code_unit.decl 8 in 0 task hierarchy "touch"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %r = simulation.context.storage %ctx[2] : !ref
      %x = simulation.context.storage %ctx[3] : !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @flop(%ctx, %clk, %q) : !simulation.context, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @echo(%ctx, %q, %r) : !simulation.context, !ref, !ref -> !simulation.process
      %p3 = simulation.spawn @pingpong(%ctx, %x) : !simulation.context, !ref -> !simulation.process
      %p4 = simulation.spawn @zero(%ctx) : !simulation.context -> !simulation.process
      %p5 = simulation.spawn @caller(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    // CHECK: phases phases valid=1
    // CHECK: @clock: ^0{1|-} ^1{1|1} ^2{-|1} ^3{-|1}
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !ref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !ref) -> ()
      cf.br ^low
    }
    // The clock's NBA lands in phase 2, so the edge-triggered body runs there.
    // CHECK: @flop: ^0{1|-} ^1{1|2} ^2{-|2}
    simulation.func private @flop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %old = simulation.ref.load %q : !ref -> !bit
      %next = simulation.logic.unary logical_not %old : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !ref) -> ()
      cf.br ^wait
    }
    // `q` changes after the flop's NBA region; a blocking echo of it changes
    // `r` in that same phase.
    // CHECK: @echo: ^0{1|-} ^1{1|3} ^2{-|3}
    simulation.func @echo(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %q to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.ref.store %v to %r : !bit, !ref
      cf.br ^wait
    }
    // A process that reschedules its own trigger through an NBA may run in
    // every later phase, but never before its first wake.
    // CHECK: @pingpong: ^0{1|-} ^1{1|-} ^2{2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32+|-}
    simulation.func @pingpong(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %x: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      %one = simulation.logic.constant true, false : !bit
      cf.br ^start
    ^start:
      simulation.nba.enqueue %one to %x : (!bit, !ref) -> ()
      simulation.suspend.change %x to ^body : !ref
    ^body:
      %v = simulation.ref.load %x : !ref -> !bit
      %n = simulation.logic.unary logical_not %v : (!bit) -> !bit
      simulation.nba.enqueue %n to %x : (!bit, !ref) -> ()
      simulation.suspend.change %x to ^body : !ref
    }
    // CHECK: @zero: ^0{1|-} ^1{any|-}
    simulation.func @zero(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 0
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
    // A nonsuspending task returns to the caller's phase-1 activation.
    // CHECK: @caller: ^0{1|-} ^1{-|1} ^2{-|1}
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %delay = simulation.time.constant 3
      %s = simulation.context.storage %ctx[4] : !ref
      simulation.suspend.delay %delay to ^call
    ^call:
      simulation.task.call @touch(%ctx, %s) arguments 2 to ^after : !simulation.context, !ref
    ^after:
      simulation.return
    }
    // CHECK: @touch: ^0{-|1}
    simulation.func private @touch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %s: !ref {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 8 : i64} {
      %one = simulation.logic.constant true, false : !bit
      simulation.ref.store %one to %s : !bit, !ref
      simulation.return
    }
    // CHECK: s0 changes {-|2}
    // CHECK: s1 changes {-|3}
    // CHECK: s2 changes {-|3}
    // CHECK: s3 changes {2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32+|-}
    // CHECK: s4 changes {-|1}
    // CHECK: s5 changes {-|-}
  }
}

// -----

// Process starts and copies. A proven copy (23.3.3) changes its destination
// exactly when its source changes, so `port` follows `clk` and the copy's
// time-zero evaluation is no update event (4.9.1). A copy of a root the root
// initializer sets may change its destination at time zero, so it is not a
// proven copy. A process forked by an activation starts in that activation's
// phases. A fork inside a plain function starts in its callers' phases,
// which the function's blocks do not carry, so it may start in any phase. A
// caller resumes after a suspending task where the task returns.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module {
  simulation.design @starts {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 4 in 0 initial hierarchy "bump"
    simulation.code_unit.decl 5 in 0 port_input hierarchy "mirror"
    simulation.code_unit.decl 6 in 0 initial hierarchy "parent"
    simulation.code_unit.decl 7 in 0 fork hierarchy "kid"
    simulation.code_unit.decl 8 in 0 function hierarchy "helper"
    simulation.code_unit.decl 9 in 0 fork hierarchy "child"
    simulation.code_unit.decl 10 in 0 initial hierarchy "waiter"
    simulation.code_unit.decl 11 in 0 task hierarchy "sync"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %port = simulation.context.storage %ctx[1] : !ref
      %seed = simulation.context.storage %ctx[2] : !ref
      %copy = simulation.context.storage %ctx[3] : !ref
      %one = simulation.logic.constant true, false : !bit
      simulation.ref.store %one to %seed : !bit, !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @copy(%ctx, %clk, %port) : !simulation.context, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @bump(%ctx, %seed) : !simulation.context, !ref -> !simulation.process
      %p3 = simulation.spawn @mirror(%ctx, %seed, %copy) : !simulation.context, !ref, !ref -> !simulation.process
      %p4 = simulation.spawn @parent(%ctx) : !simulation.context -> !simulation.process
      simulation.call @helper(%ctx) : (!simulation.context) -> ()
      %p5 = simulation.spawn @waiter(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: phases starts valid=1
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !ref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !ref) -> ()
      cf.br ^low
    }
    // CHECK: @copy: ^0{1|-} ^1{1|2}
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %port: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %clk : !ref -> !bit
      simulation.ref.store %value to %port {simulation.continuous_store} : !bit, !ref
      simulation.suspend.change %clk to ^body : !ref
    }
    simulation.func @bump(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 3
      %zero = simulation.logic.constant false, false : !bit
      simulation.suspend.delay %delay to ^write
    ^write:
      simulation.ref.store %zero to %seed : !bit, !ref
      simulation.return
    }
    simulation.func @mirror(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %copy: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %seed : !ref -> !bit
      simulation.ref.store %value to %copy {simulation.continuous_store} : !bit, !ref
      simulation.suspend.change %seed to ^body : !ref
    }
    // CHECK: @parent: ^0{1|-} ^1{-|1}
    simulation.func @parent(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^fork
    ^fork:
      %p = simulation.spawn @kid(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    // CHECK: @kid: ^0{-|1}
    simulation.func private @kid(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 13 : i32, code_unit_id = 7 : i64} {
      simulation.return
    }
    simulation.func private @helper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %p = simulation.spawn @child(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    // CHECK: @child: ^0{any|any}
    simulation.func private @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 13 : i32, code_unit_id = 9 : i64} {
      simulation.return
    }
    // CHECK: @waiter: ^0{1|-} ^1{-|1} ^2{-|2}
    simulation.func @waiter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 10 : i64} {
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^call
    ^call:
      simulation.task.call @sync(%ctx) arguments 1 to ^after : !simulation.context
    ^after:
      simulation.return
    }
    // CHECK: @sync: ^0{-|1} ^1{-|2}
    simulation.func private @sync(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 11 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      simulation.suspend.edge posedge %clk to ^done : !ref
    ^done:
      simulation.return
    }
    // CHECK: s0 changes {-|2}
    // CHECK: s1 changes {-|2}
    // CHECK: s2 changes {-|1}
    // CHECK: s3 changes {1|1}
  }
}
