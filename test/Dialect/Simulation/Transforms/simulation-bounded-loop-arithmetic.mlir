// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-mark-bounded-loops)))' | FileCheck %s --check-prefix=MARK
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-growth=64},canonicalize)))' | FileCheck %s --check-prefix=UNROLL

// IEEE 1800-2023 11.6.1, 12.7.1: fixed-width induction must reach the
// exit, including the final step. A bound below the type maximum is not
// sufficient to prove that a non-unit stride terminates.
module {
  simulation.design @induction {
    simulation.scope.decl 0
    simulation.code_unit.decl 100 in 0 initial hierarchy "accumulator"
    simulation.code_unit.decl 1 in 0 initial hierarchy "signed_wrap"
    simulation.code_unit.decl 2 in 0 initial hierarchy "unsigned_wrap"
    simulation.code_unit.decl 3 in 0 initial hierarchy "descending_wrap"
    simulation.code_unit.decl 4 in 0 initial hierarchy "inclusive_wrap"
    simulation.code_unit.decl 5 in 0 initial hierarchy "negative_stride"
    simulation.code_unit.decl 6 in 0 initial hierarchy "subtract_negative"
    simulation.code_unit.decl 7 in 0 initial hierarchy "reversed"
    simulation.code_unit.decl 8 in 0 initial hierarchy "exact_ne"
    simulation.code_unit.decl 9 in 0 initial hierarchy "unreachable_ne"
    simulation.code_unit.decl 10 in 0 initial hierarchy "modular_ne"
    simulation.code_unit.decl 11 in 0 initial hierarchy "one_eq"
    simulation.code_unit.decl 12 in 0 initial hierarchy "zero_trip_zero_stride"
    simulation.code_unit.decl 13 in 0 initial hierarchy "stuck"
    simulation.code_unit.decl 14 in 0 initial hierarchy "last_representable"

    // MARK-LABEL: simulation.func @signed_wrap(
    // MARK-NOT: bounded_loop
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @signed_wrap(
    // UNROLL: cf.cond_br
    // UNROLL: simulation.return
    simulation.func @signed_wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 127 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @unsigned_wrap(
    // MARK-NOT: bounded_loop
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @unsigned_wrap(
    // UNROLL: cf.cond_br
    // UNROLL: simulation.return
    simulation.func @unsigned_wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 255 : i8
      %test = arith.cmpi ult, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @descending_wrap(
    // MARK-NOT: bounded_loop
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @descending_wrap(
    // UNROLL: cf.cond_br
    // UNROLL: simulation.return
    simulation.func @descending_wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %start = arith.constant 255 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 0 : i8
      %test = arith.cmpi ugt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.subi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @inclusive_wrap(
    // MARK-NOT: bounded_loop
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @inclusive_wrap(
    // UNROLL: cf.cond_br
    // UNROLL: simulation.return
    simulation.func @inclusive_wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %start = arith.constant 254 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 254 : i8
      %test = arith.cmpi ule, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @negative_stride(
    // MARK: {schedule.bounded_loop_header}
    // MARK: {schedule.bounded_loop_latch}
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @negative_stride(
    // UNROLL-NOT: cf.
    // UNROLL: simulation.return
    simulation.func @negative_stride(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %start = arith.constant 5 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant -1 : i8
      %test = arith.cmpi sgt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant -2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @subtract_negative(
    // MARK: {schedule.bounded_loop_header}
    // MARK: {schedule.bounded_loop_latch}
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @subtract_negative(
    // UNROLL-NOT: cf.
    // UNROLL: simulation.return
    simulation.func @subtract_negative(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %start = arith.constant -5 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 1 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant -2 : i8
      %next = arith.subi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @reversed(
    // MARK: {schedule.bounded_loop_header}
    // MARK: {schedule.bounded_loop_latch}
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @reversed(
    // UNROLL-NOT: cf.
    // UNROLL: simulation.return
    simulation.func @reversed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 3 : i8
      %test = arith.cmpi ugt, %bound, %i : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 1 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @exact_ne(
    // MARK: {schedule.bounded_loop_header}
    // MARK: {schedule.bounded_loop_latch}
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @exact_ne(
    // UNROLL-NOT: cf.
    // UNROLL: simulation.return
    simulation.func @exact_ne(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 6 : i8
      %test = arith.cmpi ne, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @unreachable_ne(
    // MARK-NOT: bounded_loop
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @unreachable_ne(
    // UNROLL: cf.cond_br
    // UNROLL: simulation.return
    simulation.func @unreachable_ne(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 7 : i8
      %test = arith.cmpi ne, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @modular_ne(
    // MARK: {schedule.bounded_loop_header}
    // MARK: {schedule.bounded_loop_latch}
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @modular_ne(
    // UNROLL-NOT: cf.
    // UNROLL: simulation.return
    simulation.func @modular_ne(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 10 : i64} {
      %start = arith.constant 254 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 2 : i8
      %test = arith.cmpi ne, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @one_eq(
    // MARK: {schedule.bounded_loop_header}
    // MARK: {schedule.bounded_loop_latch}
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @one_eq(
    // UNROLL-NOT: cf.
    // UNROLL: simulation.return
    simulation.func @one_eq(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 11 : i64} {
      %start = arith.constant 3 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 3 : i8
      %test = arith.cmpi eq, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @zero_trip_zero_stride(
    // MARK: {schedule.bounded_loop_header}
    // MARK: {schedule.bounded_loop_latch}
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @zero_trip_zero_stride(
    // UNROLL-NOT: cf.
    // UNROLL: simulation.return
    simulation.func @zero_trip_zero_stride(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 12 : i64} {
      %start = arith.constant 4 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 3 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 0 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @stuck(
    // MARK-NOT: bounded_loop
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @stuck(
    // UNROLL: cf.cond_br
    // UNROLL: simulation.return
    simulation.func @stuck(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 13 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 3 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 0 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @last_representable(
    // MARK: {schedule.bounded_loop_header}
    // MARK: {schedule.bounded_loop_latch}
    // MARK: simulation.return
    // UNROLL-LABEL: simulation.func @last_representable(
    // UNROLL-NOT: cf.
    // UNROLL: simulation.return
    simulation.func @last_representable(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 14 : i64} {
      %start = arith.constant 123 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %bound = arith.constant 127 : i8
      %test = arith.cmpi slt, %i, %bound : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : i8, !simulation.ref<i8>
      %step = arith.constant 2 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^exit:
      simulation.return
    }
    // MARK-LABEL: simulation.func @accumulator(
    // MARK: {schedule.bounded_loop_header}
    // MARK: {schedule.bounded_loop_latch}
    // UNROLL-LABEL: simulation.func @accumulator(
    // UNROLL: %[[SIX:.*]] = arith.constant 6 : i32
    // UNROLL-NOT: cf.
    // UNROLL: simulation.ref.store %[[SIX]]
    // UNROLL: simulation.return
    simulation.func @accumulator(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 100 : i64} {
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
      simulation.ref.store %result to %out : i32, !simulation.ref<i32>
      simulation.return
    }
  }
}
