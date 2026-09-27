// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-mark-bounded-loops)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops)))' | FileCheck %s --check-prefix=UNROLL

// Leaving a constant-induction loop early only shortens the sweep: a `break`
// to the loop exit (IEEE 1800-2023 12.8), or the `$finish` check an inlined
// call leaves behind (20.2), cannot make the latch run more often than the
// for-loop test allows (12.7.1). A
// value carried around the loop beside the induction variable, such as an
// `x ^= a[i]` accumulator promoted to SSA, cannot either. Such loops keep their
// termination proof. Replication needs a single exit and leaves them alone.
// An exit that re-enters the loop is not early and stays unproven.
module {
  simulation.design @loops {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "brk"
    simulation.code_unit.decl 2 in 0 initial hierarchy "finish"
    simulation.code_unit.decl 3 in 0 initial hierarchy "nested"
    simulation.code_unit.decl 4 in 0 initial hierarchy "reentry"
    simulation.code_unit.decl 5 in 0 initial hierarchy "carried"

    // CHECK-LABEL: simulation.func @brk
    // CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}} {schedule.bounded_loop_header}
    // CHECK: cf.br ^{{.*}}(%{{.*}} : i32) {schedule.bounded_loop_latch}
    // UNROLL-LABEL: simulation.func @brk
    // UNROLL: arith.cmpi slt
    // UNROLL: cf.cond_br
    simulation.func @brk(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %req: !simulation.ref<i4> {simulation.capture_kind = 1 : i32}, %sel: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 4 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %bits = simulation.ref.load %req : !simulation.ref<i4> -> i4
      %lane = arith.trunci %i : i32 to i4
      %shifted = arith.shrui %bits, %lane : i4
      %hit = arith.trunci %shifted : i4 to i1
      cf.cond_br %hit, ^found, ^latch
    ^found:
      simulation.ref.store %i to %sel : i32, !simulation.ref<i32>
      cf.br ^exit
    ^latch:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @finish
    // CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}} {schedule.bounded_loop_header}
    // CHECK: cf.br ^{{.*}}(%{{.*}} : i32) {schedule.bounded_loop_latch}
    // UNROLL-LABEL: simulation.func @finish
    // UNROLL: arith.cmpi slt
    // UNROLL: simulation.termination.requested
    simulation.func @finish(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 4 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %stop = simulation.termination.requested %ctx
      cf.cond_br %stop, ^terminate, ^latch
    ^terminate:
      simulation.return
    ^latch:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // The inner sweep breaks out; once it is proven, the outer sweep over it
    // is a DAG apart from its own backedge.
    // CHECK-LABEL: simulation.func @nested
    // CHECK-COUNT-2: {schedule.bounded_loop_latch}
    simulation.func @nested(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %req: !simulation.ref<i4> {simulation.capture_kind = 1 : i32}, %sel: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%p: i32):
      %plimit = arith.constant 2 : i32
      %ptest = arith.cmpi slt, %p, %plimit : i32
      cf.cond_br %ptest, ^inner_entry, ^exit
    ^inner_entry:
      %izero = arith.constant 0 : i32
      cf.br ^inner(%izero : i32)
    ^inner(%r: i32):
      %rlimit = arith.constant 4 : i32
      %rtest = arith.cmpi slt, %r, %rlimit : i32
      cf.cond_br %rtest, ^inner_body, ^outer_latch
    ^inner_body:
      %bits = simulation.ref.load %req : !simulation.ref<i4> -> i4
      %lane = arith.trunci %r : i32 to i4
      %shifted = arith.shrui %bits, %lane : i4
      %hit = arith.trunci %shifted : i4 to i1
      cf.cond_br %hit, ^grant, ^inner_latch
    ^grant:
      simulation.ref.store %r to %sel : i32, !simulation.ref<i32>
      cf.br ^outer_latch
    ^inner_latch:
      %rone = arith.constant 1 : i32
      %rnext = arith.addi %r, %rone : i32
      cf.br ^inner(%rnext : i32)
    ^outer_latch:
      %pone = arith.constant 1 : i32
      %pnext = arith.addi %p, %pone : i32
      cf.br ^outer(%pnext : i32)
    ^exit:
      simulation.return
    }

    // The body jumps back to the block that enters the loop, so the loop can
    // restart its induction and has no bound.
    // CHECK-LABEL: simulation.func @reentry
    // CHECK-NOT: bounded_loop
    // CHECK: simulation.return
    simulation.func @reentry(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %flag: !simulation.ref<i1> {simulation.capture_kind = 1 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      cf.br ^start
    ^start:
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 4 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %again = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      cf.cond_br %again, ^start, ^latch
    ^latch:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @carried
    // CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}} {schedule.bounded_loop_header}
    // CHECK: cf.br ^{{.*}}(%{{.*}}, %{{.*}} : i32, i8) {schedule.bounded_loop_latch}
    // UNROLL-LABEL: simulation.func @carried
    // UNROLL: arith.cmpi slt
    simulation.func @carried(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %zero = arith.constant 0 : i32
      %seed = arith.constant 0 : i8
      cf.br ^head(%zero, %seed : i32, i8)
    ^head(%i: i32, %acc: i8):
      %limit = arith.constant 4 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %mixed = arith.xori %acc, %v : i8
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next, %mixed : i32, i8)
    ^exit:
      simulation.ref.store %acc to %out : i8, !simulation.ref<i8>
      simulation.return
    }
  }
}
