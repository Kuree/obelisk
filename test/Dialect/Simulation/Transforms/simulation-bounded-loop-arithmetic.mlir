// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-mark-bounded-loops)))' | FileCheck %s --check-prefix=MARK
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-unroll-bounded-loops,canonicalize)))' | FileCheck %s --check-prefix=UNROLL

// IEEE 1800-2023 11.6.1, 12.7.1: fixed-width induction must reach the
// exit, including the final step. A bound below the type maximum is not
// sufficient to prove that a non-unit stride terminates.
module {
  obelisk_sim.design @induction {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 100 in 0 initial hierarchy "accumulator"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "signed_wrap"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "unsigned_wrap"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "descending_wrap"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "inclusive_wrap"
    obelisk_sim.code_unit.decl 5 in 0 initial hierarchy "negative_stride"
    obelisk_sim.code_unit.decl 6 in 0 initial hierarchy "subtract_negative"
    obelisk_sim.code_unit.decl 7 in 0 initial hierarchy "reversed"
    obelisk_sim.code_unit.decl 8 in 0 initial hierarchy "exact_ne"
    obelisk_sim.code_unit.decl 9 in 0 initial hierarchy "unreachable_ne"
    obelisk_sim.code_unit.decl 10 in 0 initial hierarchy "modular_ne"
    obelisk_sim.code_unit.decl 11 in 0 initial hierarchy "one_eq"
    obelisk_sim.code_unit.decl 12 in 0 initial hierarchy "zero_trip_zero_stride"
    obelisk_sim.code_unit.decl 13 in 0 initial hierarchy "stuck"
    obelisk_sim.code_unit.decl 14 in 0 initial hierarchy "last_representable"

    // MARK-LABEL: obelisk_sim.func @signed_wrap(
    // MARK-NOT: bounded_loop
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @signed_wrap(
    // UNROLL: cf.cond_br
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @signed_wrap(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 127 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @unsigned_wrap(
    // MARK-NOT: bounded_loop
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @unsigned_wrap(
    // UNROLL: cf.cond_br
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @unsigned_wrap(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 255 : i8
      %test = arith.cmpi ult, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @descending_wrap(
    // MARK-NOT: bounded_loop
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @descending_wrap(
    // UNROLL: cf.cond_br
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @descending_wrap(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %start = arith.constant 255 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 0 : i8
      %test = arith.cmpi ugt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.subi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @inclusive_wrap(
    // MARK-NOT: bounded_loop
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @inclusive_wrap(
    // UNROLL: cf.cond_br
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @inclusive_wrap(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %start = arith.constant 254 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 254 : i8
      %test = arith.cmpi ule, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @negative_stride(
    // MARK: {obelisk_sim.bounded_loop_header}
    // MARK: {obelisk_sim.bounded_loop_latch}
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @negative_stride(
    // UNROLL-NOT: cf.
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @negative_stride(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %start = arith.constant 5 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant -1 : i8
      %test = arith.cmpi sgt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant -2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @subtract_negative(
    // MARK: {obelisk_sim.bounded_loop_header}
    // MARK: {obelisk_sim.bounded_loop_latch}
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @subtract_negative(
    // UNROLL-NOT: cf.
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @subtract_negative(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %start = arith.constant -5 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 1 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant -2 : i8
      %next = arith.subi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @reversed(
    // MARK: {obelisk_sim.bounded_loop_header}
    // MARK: {obelisk_sim.bounded_loop_latch}
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @reversed(
    // UNROLL-NOT: cf.
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @reversed(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 3 : i8
      %test = arith.cmpi ugt, %bound, %i : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 1 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @exact_ne(
    // MARK: {obelisk_sim.bounded_loop_header}
    // MARK: {obelisk_sim.bounded_loop_latch}
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @exact_ne(
    // UNROLL-NOT: cf.
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @exact_ne(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 6 : i8
      %test = arith.cmpi ne, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @unreachable_ne(
    // MARK-NOT: bounded_loop
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @unreachable_ne(
    // UNROLL: cf.cond_br
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @unreachable_ne(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 7 : i8
      %test = arith.cmpi ne, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @modular_ne(
    // MARK: {obelisk_sim.bounded_loop_header}
    // MARK: {obelisk_sim.bounded_loop_latch}
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @modular_ne(
    // UNROLL-NOT: cf.
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @modular_ne(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 10 : i64} {
      %start = arith.constant 254 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 2 : i8
      %test = arith.cmpi ne, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @one_eq(
    // MARK: {obelisk_sim.bounded_loop_header}
    // MARK: {obelisk_sim.bounded_loop_latch}
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @one_eq(
    // UNROLL-NOT: cf.
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @one_eq(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 11 : i64} {
      %start = arith.constant 3 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 3 : i8
      %test = arith.cmpi eq, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @zero_trip_zero_stride(
    // MARK: {obelisk_sim.bounded_loop_header}
    // MARK: {obelisk_sim.bounded_loop_latch}
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @zero_trip_zero_stride(
    // UNROLL-NOT: cf.
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @zero_trip_zero_stride(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 12 : i64} {
      %start = arith.constant 4 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 3 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 0 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @stuck(
    // MARK-NOT: bounded_loop
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @stuck(
    // UNROLL: cf.cond_br
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @stuck(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 13 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 3 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 0 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }

    // MARK-LABEL: obelisk_sim.func @last_representable(
    // MARK: {obelisk_sim.bounded_loop_header}
    // MARK: {obelisk_sim.bounded_loop_latch}
    // MARK: obelisk_sim.return
    // UNROLL-LABEL: obelisk_sim.func @last_representable(
    // UNROLL-NOT: cf.
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @last_representable(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 14 : i64} {
      %start = arith.constant 123 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 127 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      obelisk_sim.ref.store %i to %out : i8, !obelisk_sim.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      obelisk_sim.return
    }
    // MARK-LABEL: obelisk_sim.func @accumulator(
    // MARK: {obelisk_sim.bounded_loop_header}
    // MARK: {obelisk_sim.bounded_loop_latch}
    // UNROLL-LABEL: obelisk_sim.func @accumulator(
    // UNROLL: %[[SIX:.*]] = arith.constant 6 : i32
    // UNROLL-NOT: cf.
    // UNROLL: obelisk_sim.ref.store %[[SIX]]
    // UNROLL: obelisk_sim.return
    obelisk_sim.func @accumulator(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %out: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 100 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero, %zero : i32, i32)
    ^head(%sum: i32, %i: i32):
      %bound = arith.constant 4 : i32
      %test = arith.cmpi slt, %i, %bound : i32
      cf.cond_br %test, ^body, ^exit(%sum : i32)
    ^body:
      %new_sum = arith.addi %sum, %i : i32
      %one = arith.constant 1 : i32
      %next = arith.addi %one, %i : i32
      cf.br ^head(%new_sum, %next : i32, i32)
    ^exit(%result: i32):
      obelisk_sim.ref.store %result to %out : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }
  }
}
