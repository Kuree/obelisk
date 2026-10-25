// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-mark-bounded-loops)))' | FileCheck %s --check-prefix=MARK

// Keep the existing CFG, SSA, finite/wrap and width restrictions. In
// particular, signed comparison of a widened value does not establish the
// same signed recurrence at the induction's original width (LRM 11.6.1).
!counter = !simulation.logic<8>
module {
  simulation.design @logic_rejections {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "unknown_init"
    simulation.code_unit.decl 2 in 0 function hierarchy "unknown_bound"
    simulation.code_unit.decl 3 in 0 function hierarchy "unknown_step"
    simulation.code_unit.decl 4 in 0 function hierarchy "signed_wrap"
    simulation.code_unit.decl 5 in 0 function hierarchy "zero_step"
    simulation.code_unit.decl 6 in 0 function hierarchy "signed_widened_test"
    simulation.code_unit.decl 7 in 0 function hierarchy "unsigned_widened_test"
    simulation.code_unit.decl 8 in 0 function hierarchy "signed_widened_step"
    simulation.code_unit.decl 9 in 0 function hierarchy "unsigned_widened_step"
    simulation.code_unit.decl 10 in 0 function hierarchy "escaping_induction"
    simulation.code_unit.decl 11 in 0 function hierarchy "header_observer"
    simulation.code_unit.decl 12 in 0 function hierarchy "different_entries"
    simulation.code_unit.decl 13 in 0 task hierarchy "suspension"

    // CHECK-LABEL: simulation.func @unknown_init
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @unknown_init
    // MARK-NOT: schedule.bounded_loop_
    // MARK: simulation.return
    simulation.func @unknown_init(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %start = simulation.logic.constant 0 : i8, 1 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 3 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @unknown_bound
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @unknown_bound
    // MARK-NOT: schedule.bounded_loop_
    // MARK: simulation.return
    simulation.func @unknown_bound(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %start = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 3 : i8, 1 : i8 : !counter
      %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @unknown_step
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @unknown_step
    // MARK-NOT: schedule.bounded_loop_
    // MARK: simulation.return
    simulation.func @unknown_step(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %start = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 3 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %step = simulation.logic.constant 1 : i8, 1 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // The first step wraps, and the even-valued recurrence never reaches 127.
    // CHECK-LABEL: simulation.func @signed_wrap
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @signed_wrap
    // MARK-NOT: schedule.bounded_loop_
    // MARK: simulation.return
    simulation.func @signed_wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %start = simulation.logic.constant 126 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 127 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %step = simulation.logic.constant 2 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @zero_step
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @zero_step
    // MARK-NOT: schedule.bounded_loop_
    // MARK: simulation.return
    simulation.func @zero_step(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %start = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 3 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %step = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // Even a zero-extension wrapper must not inherit a signed recurrence
    // proof: the test's sign bit is no longer the stored counter's sign bit.
    // CHECK-LABEL: simulation.func @signed_widened_test
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @signed_widened_test
    // MARK-NOT: schedule.bounded_loop_
    // MARK: simulation.return
    simulation.func @signed_widened_test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %start = simulation.logic.constant -2 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %wide = simulation.logic.resize %i signed = false : !counter -> !simulation.logic<16>
      %limit = simulation.logic.constant 1 : i16, 0 : i16 : !simulation.logic<16>
      %comparison = simulation.logic.compare sle %wide, %limit : (!simulation.logic<16>, !simulation.logic<16>) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // Existing unsigned wrapper termination support remains available, but
    // replication is limited to direct same-width logic tests and updates.
    // CHECK-LABEL: simulation.func @unsigned_widened_test
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @unsigned_widened_test
    // MARK: schedule.bounded_loop_header
    // MARK: schedule.bounded_loop_latch
    simulation.func @unsigned_widened_test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %start = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %wide = simulation.logic.resize %i signed = false : !counter -> !simulation.logic<16>
      %limit = simulation.logic.constant 3 : i16, 0 : i16 : !simulation.logic<16>
      %comparison = simulation.logic.compare ult %wide, %limit : (!simulation.logic<16>, !simulation.logic<16>) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @signed_widened_step
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @signed_widened_step
    // MARK-NOT: schedule.bounded_loop_
    // MARK: simulation.return
    simulation.func @signed_widened_step(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %start = simulation.logic.constant -2 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %wide = simulation.logic.resize %i signed = true : !counter -> !simulation.logic<16>
      %step = simulation.logic.constant 1 : i16, 0 : i16 : !simulation.logic<16>
      %sum = simulation.logic.binary add %wide, %step : !simulation.logic<16>
      %next = simulation.logic.extract %sum from 0 : !simulation.logic<16> -> !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @unsigned_widened_step
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @unsigned_widened_step
    // MARK: schedule.bounded_loop_header
    // MARK: schedule.bounded_loop_latch
    simulation.func @unsigned_widened_step(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %start = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 3 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare ult %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %wide = simulation.logic.resize %i signed = false : !counter -> !simulation.logic<16>
      %step = simulation.logic.constant 1 : i16, 0 : i16 : !simulation.logic<16>
      %sum = simulation.logic.binary add %wide, %step : !simulation.logic<16>
      %next = simulation.logic.extract %sum from 0 : !simulation.logic<16> -> !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // Direct uses of a removed block argument still require SSA repair.
    // CHECK-LABEL: simulation.func @escaping_induction
    // CHECK: cf.cond_br
    // CHECK: cf.br
    // CHECK: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @escaping_induction
    // MARK: schedule.bounded_loop_header
    // MARK: schedule.bounded_loop_latch
    simulation.func @escaping_induction(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %start = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 3 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      simulation.return
    }

    // Preserve the restriction on effects in a repeatedly evaluated header.
    // CHECK-LABEL: simulation.func @header_observer
    // CHECK: simulation.ref.store
    // CHECK: cf.cond_br
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @header_observer
    // MARK: schedule.bounded_loop_header
    // MARK: schedule.bounded_loop_latch
    simulation.func @header_observer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %start = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %limit = simulation.logic.constant 3 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @different_entries
    // CHECK: cf.cond_br
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @different_entries
    // MARK-NOT: schedule.bounded_loop_
    // MARK: simulation.return
    simulation.func @different_entries(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}, %select: i1 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      cf.cond_br %select, ^head(%zero : !counter), ^head(%one : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 3 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %next = simulation.logic.binary add %i, %one : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // CHECK-LABEL: simulation.func @suspension
    // CHECK: cf.cond_br
    // CHECK: simulation.suspend.delay
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // MARK-LABEL: simulation.func @suspension
    // MARK-NOT: schedule.bounded_loop_
    // MARK: simulation.return
    simulation.func @suspension(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 13 : i64} {
      %start = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 3 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare slt %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^latch
    ^latch:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }
  }
}
