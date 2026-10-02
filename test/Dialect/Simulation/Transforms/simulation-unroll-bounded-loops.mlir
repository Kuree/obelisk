// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-growth=64},canonicalize)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops,canonicalize)))' | FileCheck %s --check-prefix=COMPACT

// The induction loop is finite within one activation, even with a conditional
// body. Its exact sequence of publications must survive unrolling. Dynamic
// bounds, modular wraparound, and suspension remain scheduler-owned CFGs.
module {
  simulation.design @loops {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "ascending"
    simulation.code_unit.decl 2 in 0 initial hierarchy "descending"
    simulation.code_unit.decl 3 in 0 initial hierarchy "zero_trip"
    simulation.code_unit.decl 4 in 0 initial hierarchy "dynamic"
    simulation.code_unit.decl 5 in 0 initial hierarchy "wrap"
    simulation.code_unit.decl 6 in 0 initial hierarchy "conditional"
    simulation.code_unit.decl 7 in 0 initial hierarchy "timed"
    simulation.code_unit.decl 8 in 0 initial hierarchy "side_exit"
    simulation.code_unit.decl 9 in 0 initial hierarchy "large"
    // CHECK-LABEL: simulation.func @ascending
    // COMPACT-LABEL: simulation.func @ascending
    // COMPACT: cf.cond_br
    // COMPACT: simulation.ref.store
    // COMPACT-NOT: simulation.ref.store
    // COMPACT: simulation.return
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}}
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}}
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}}
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    simulation.func @ascending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},  %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 3 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @descending
    // CHECK-DAG: %[[TWO:.*]] = arith.constant 2 : i32
    // CHECK-DAG: %[[ONE:.*]] = arith.constant 1 : i32
    // CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
    // CHECK: simulation.ref.store %[[TWO]]
    // CHECK: simulation.ref.store %[[ONE]]
    // CHECK: simulation.ref.store %[[ZERO]]
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    simulation.func @descending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},  %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %two = arith.constant 2 : i32
      cf.br ^head(%two : i32)
    ^head(%i: i32):
      %zero = arith.constant 0 : i32
      %test = arith.cmpi sge, %i, %zero : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.subi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @zero_trip
    // CHECK-NOT: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    simulation.func @zero_trip(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},  %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 0 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @dynamic
    // CHECK: arith.cmpi ult
    // CHECK: cf.cond_br
    simulation.func @dynamic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},  %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}, %limit: i32 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @wrap
    // CHECK: arith.cmpi ule
    // CHECK: cf.cond_br
    simulation.func @wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},  %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %start = arith.constant 254 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %limit = arith.constant 254 : i8
      %test = arith.cmpi ule, %i, %limit : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %one = arith.constant 2 : i8
      %next = arith.addi %i, %one : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @conditional
    // CHECK-NOT: arith.cmpi
    // CHECK-COUNT-2: cf.cond_br
    // CHECK: simulation.return
    simulation.func @conditional(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},  %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}, %select: i1 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      cf.cond_br %select, ^write, ^latch
    ^latch:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^write:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      cf.br ^latch
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @timed
    // CHECK: arith.cmpi ult
    // CHECK: cf.cond_br
    // CHECK: simulation.suspend.delay
    simulation.func @timed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},  %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^latch
    ^latch:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // A break-like side exit invalidates the single-exit proof.
    // CHECK-LABEL: simulation.func @side_exit
    // CHECK: arith.cmpi ult
    // CHECK: cf.cond_br
    simulation.func @side_exit(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},  %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}, %break: i1 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      cf.cond_br %break, ^exit, ^latch
    ^latch:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // Unrolling is bounded independently of the input's valid trip count.
    // CHECK-LABEL: simulation.func @large
    // CHECK: arith.cmpi ult
    // CHECK: cf.cond_br
    simulation.func @large(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},  %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 1000 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
