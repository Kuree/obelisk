// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-unroll-bounded-loops,canonicalize)))' | FileCheck %s

// The induction loop is finite within one activation, even with a conditional
// body. Its exact sequence of publications must survive unrolling. Dynamic
// bounds, modular wraparound, and suspension remain scheduler-owned CFGs.
module {
  obelisk_sim.design @loops {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "ascending"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "descending"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "zero_trip"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "dynamic"
    obelisk_sim.code_unit.decl 5 in 0 initial hierarchy "wrap"
    obelisk_sim.code_unit.decl 6 in 0 initial hierarchy "conditional"
    obelisk_sim.code_unit.decl 7 in 0 initial hierarchy "timed"
    obelisk_sim.code_unit.decl 8 in 0 initial hierarchy "side_exit"
    obelisk_sim.code_unit.decl 9 in 0 initial hierarchy "large"
    // CHECK-LABEL: obelisk_sim.func @ascending
    // CHECK: obelisk_sim.ref.store %{{.*}} to %{{.*}}
    // CHECK: obelisk_sim.ref.store %{{.*}} to %{{.*}}
    // CHECK: obelisk_sim.ref.store %{{.*}} to %{{.*}}
    // CHECK-NOT: cf.
    // CHECK: obelisk_sim.return
    obelisk_sim.func @ascending(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},  %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 3 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @descending
    // CHECK-DAG: %[[TWO:.*]] = arith.constant 2 : i32
    // CHECK-DAG: %[[ONE:.*]] = arith.constant 1 : i32
    // CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
    // CHECK: obelisk_sim.ref.store %[[TWO]]
    // CHECK: obelisk_sim.ref.store %[[ONE]]
    // CHECK: obelisk_sim.ref.store %[[ZERO]]
    // CHECK-NOT: cf.
    // CHECK: obelisk_sim.return
    obelisk_sim.func @descending(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},  %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %two = arith.constant 2 : i32
      cf.br ^head(%two : i32)
    ^head(%i: i32):
      %zero = arith.constant 0 : i32
      %test = arith.cmpi sge, %i, %zero : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.subi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @zero_trip
    // CHECK-NOT: obelisk_sim.ref.store
    // CHECK-NOT: cf.
    // CHECK: obelisk_sim.return
    obelisk_sim.func @zero_trip(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},  %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 0 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @dynamic
    // CHECK: arith.cmpi ult
    // CHECK: cf.cond_br
    obelisk_sim.func @dynamic(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},  %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}, %limit: i32 {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @wrap
    // CHECK: arith.cmpi ule
    // CHECK: cf.cond_br
    obelisk_sim.func @wrap(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},  %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %start = arith.constant 254 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %limit = arith.constant 254 : i8
      %test = arith.cmpi ule, %i, %limit : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %one = arith.constant 2 : i8
      %next = arith.addi %i, %one : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @conditional
    // CHECK-NOT: arith.cmpi
    // CHECK-COUNT-2: cf.cond_br
    // CHECK: obelisk_sim.return
    obelisk_sim.func @conditional(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},  %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}, %select: i1 {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
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
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      cf.br ^latch
    ^exit:
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @timed
    // CHECK: arith.cmpi ult
    // CHECK: cf.cond_br
    // CHECK: obelisk_sim.suspend.delay
    obelisk_sim.func @timed(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},  %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^latch
    ^latch:
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      obelisk_sim.return
    }

    // A break-like side exit invalidates the single-exit proof.
    // CHECK-LABEL: obelisk_sim.func @side_exit
    // CHECK: arith.cmpi ult
    // CHECK: cf.cond_br
    obelisk_sim.func @side_exit(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},  %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}, %break: i1 {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      cf.cond_br %break, ^exit, ^latch
    ^latch:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      obelisk_sim.return
    }

    // Unrolling is bounded independently of the input's valid trip count.
    // CHECK-LABEL: obelisk_sim.func @large
    // CHECK: arith.cmpi ult
    // CHECK: cf.cond_br
    obelisk_sim.func @large(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},  %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 1000 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i32, !obelisk_sim.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      obelisk_sim.return
    }
  }
}
