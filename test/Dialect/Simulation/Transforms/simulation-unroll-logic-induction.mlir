// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-mark-bounded-loops)))' | FileCheck %s --check-prefix=MARK
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops,canonicalize,cse)))' | FileCheck %s
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops,canonicalize,cse)))' -o %t.serial
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops,canonicalize,cse,obelisk-sim-unroll-bounded-loops)))' -o %t.twice
// RUN: diff -u %t.serial %t.twice

// IEEE 1800-2023 4.6, 11.6.1, 12.7.1: a known four-state induction has
// the same fixed-width sequence, including its signed negative values. Each
// store remains before its observer, and each iteration before the next one.
// The paired Runtime test executes this MLIR with and without replication.
!counter = !simulation.logic<8>
!element = !simulation.logic<79>
!array = !simulation.packed_array<6 : 0 x !element>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @logic_induction {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !counter design
    simulation.storage.decl 1 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "descending"
    simulation.code_unit.decl 2 in 0 function hierarchy "ascending"
    simulation.code_unit.decl 3 in 0 function hierarchy "zero_trip"
    simulation.code_unit.decl 4 in 0 function hierarchy "packed_copy"
    simulation.code_unit.decl 5 in 0 function hierarchy "observe_counter"
    simulation.code_unit.decl 6 in 0 function hierarchy "observe_element"
    simulation.code_unit.decl 7 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 8 in 0 function hierarchy "unsigned_direct"

    // MARK-LABEL: simulation.func @descending
    // MARK: schedule.bounded_loop_header
    // MARK: schedule.bounded_loop_latch
    // CHECK-LABEL: simulation.func @descending
    // CHECK-DAG: %[[ONE:.*]] = simulation.logic.constant 1 : i8, 0 : i8
    // CHECK-DAG: %[[NEG_ONE:.*]] = simulation.logic.constant -1 : i8, 0 : i8
    // CHECK-DAG: %[[NEG_THREE:.*]] = simulation.logic.constant -3 : i8, 0 : i8
    // CHECK: simulation.ref.store %[[ONE]]
    // CHECK-NEXT: simulation.call @observe_counter
    // CHECK-NEXT: simulation.ref.store %[[NEG_ONE]]
    // CHECK-NEXT: simulation.call @observe_counter
    // CHECK-NEXT: simulation.ref.store %[[NEG_THREE]]
    // CHECK-NEXT: simulation.call @observe_counter
    // CHECK-NEXT: simulation.return
    simulation.func @descending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %start = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant -3 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare sge %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      simulation.call @observe_counter(%ctx, %out) : (!simulation.context, !simulation.ref<!counter>) -> ()
      %step = simulation.logic.constant -2 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %step, %i : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @ascending
    // MARK: schedule.bounded_loop_header
    // MARK: schedule.bounded_loop_latch
    // CHECK-LABEL: simulation.func @ascending
    // CHECK-DAG: %[[NEG_THREE:.*]] = simulation.logic.constant -3 : i8, 0 : i8
    // CHECK-DAG: %[[NEG_ONE:.*]] = simulation.logic.constant -1 : i8, 0 : i8
    // CHECK-DAG: %[[ONE:.*]] = simulation.logic.constant 1 : i8, 0 : i8
    // CHECK: simulation.ref.store %[[NEG_THREE]]
    // CHECK-NEXT: simulation.call @observe_counter
    // CHECK-NEXT: simulation.ref.store %[[NEG_ONE]]
    // CHECK-NEXT: simulation.call @observe_counter
    // CHECK-NEXT: simulation.ref.store %[[ONE]]
    // CHECK-NEXT: simulation.call @observe_counter
    // CHECK-NEXT: simulation.return
    simulation.func @ascending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %start = simulation.logic.constant -3 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      simulation.call @observe_counter(%ctx, %out) : (!simulation.context, !simulation.ref<!counter>) -> ()
      %step = simulation.logic.constant -2 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary sub %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @zero_trip
    // MARK: schedule.bounded_loop_header
    // MARK: schedule.bounded_loop_latch
    // CHECK-LABEL: simulation.func @zero_trip
    // CHECK-NEXT: simulation.return
    simulation.func @zero_trip(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %start = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant -1 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      simulation.call @observe_counter(%ctx, %out) : (!simulation.context, !simulation.ref<!counter>) -> ()
      %step = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    // Source-shaped six-iteration packed copy. Iteration zero must not read
    // index -1; later loads alias earlier stores and must see their new X/Z
    // payloads. The seventh 79-bit element is an untouched neighbor.
    // MARK-LABEL: simulation.func @packed_copy
    // MARK: schedule.bounded_loop_header
    // MARK: schedule.bounded_loop_latch
    // CHECK-LABEL: simulation.func @packed_copy
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.ref.subelement {{.*}}[6]
    // CHECK: simulation.ref.store
    // CHECK-NEXT: simulation.call @observe_element
    // CHECK: simulation.ref.subelement {{.*}}[5]
    // CHECK: simulation.ref.store
    // CHECK-NEXT: simulation.call @observe_element
    // CHECK: simulation.ref.subelement {{.*}}[4]
    // CHECK: simulation.ref.store
    // CHECK-NEXT: simulation.call @observe_element
    // CHECK: simulation.ref.subelement {{.*}}[3]
    // CHECK: simulation.ref.store
    // CHECK-NEXT: simulation.call @observe_element
    // CHECK: simulation.ref.subelement {{.*}}[2]
    // CHECK: simulation.ref.store
    // CHECK-NEXT: simulation.call @observe_element
    // CHECK: simulation.ref.subelement {{.*}}[1]
    // CHECK: simulation.ref.store
    // CHECK-NEXT: simulation.call @observe_element
    // CHECK-NEXT: simulation.return
    simulation.func @packed_copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %array: !simulation.ref<!array> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %zero = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      %seed = simulation.logic.constant 5 : i79, 3 : i79 : !element
      cf.br ^head(%zero : !simulation.logic<32>)
    ^head(%i: !simulation.logic<32>):
      %limit = simulation.logic.constant 5 : i32, 0 : i32 : !simulation.logic<32>
      %comparison = simulation.logic.compare sle %i, %limit : (!simulation.logic<32>, !simulation.logic<32>) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      %first = simulation.logic.compare case_eq %i, %zero : (!simulation.logic<32>, !simulation.logic<32>) -> i1
      cf.cond_br %first, ^initial, ^copy
    ^initial:
      cf.br ^write(%seed : !element)
    ^copy:
      %source_one = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %previous = simulation.logic.binary sub %i, %source_one : !simulation.logic<32>
      %source_index = simulation.logic.resize %previous signed = true : !simulation.logic<32> -> !simulation.logic<65>
      %source = simulation.ref.array_element %array[%source_index] : (!simulation.ref<!array>, !simulation.logic<65>) -> !simulation.ref<!element>
      %loaded = simulation.ref.load %source : !simulation.ref<!element> -> !element
      cf.br ^write(%loaded : !element)
    ^write(%value: !element):
      %destination_index = simulation.logic.resize %i signed = true : !simulation.logic<32> -> !simulation.logic<65>
      %destination = simulation.ref.array_element %array[%destination_index] : (!simulation.ref<!array>, !simulation.logic<65>) -> !simulation.ref<!element>
      simulation.ref.store %value to %destination : !element, !simulation.ref<!element>
      simulation.call @observe_element(%ctx, %destination, %seed) : (!simulation.context, !simulation.ref<!element>, !element) -> ()
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %next = simulation.logic.binary add %i, %one : !simulation.logic<32>
      cf.br ^head(%next : !simulation.logic<32>)
    ^exit:
      simulation.return
    }

    // MARK-LABEL: simulation.func @unsigned_direct
    // MARK: schedule.bounded_loop_header
    // MARK: schedule.bounded_loop_latch
    // CHECK-LABEL: simulation.func @unsigned_direct
    // CHECK-COUNT-3: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    simulation.func @unsigned_direct(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %start = simulation.logic.constant 250 : i8, 0 : i8 : !counter
      cf.br ^head(%start : !counter)
    ^head(%i: !counter):
      %limit = simulation.logic.constant 252 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare ule %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit
    ^body:
      simulation.ref.store %i to %out : !counter, !simulation.ref<!counter>
      simulation.call @observe_counter(%ctx, %out) : (!simulation.context, !simulation.ref<!counter>) -> ()
      %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next : !counter)
    ^exit:
      simulation.return
    }

    simulation.func @observe_counter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!counter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %value = simulation.ref.load %out : !simulation.ref<!counter> -> !counter
      %format = simulation.bytes.constant "visit %h"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%format, %value) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !counter
      simulation.return
    }
    simulation.func @observe_element(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<!element> {simulation.capture_kind = 1 : i32}, %expected: !element {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %value = simulation.ref.load %out : !simulation.ref<!element> -> !element
      %same = simulation.logic.compare case_eq %value, %expected : (!element, !element) -> i1
      %format = simulation.bytes.constant "packed %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%format, %same) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i1
      simulation.return
    }
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 7 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!counter>
      simulation.call @descending(%ctx, %out) : (!simulation.context, !simulation.ref<!counter>) -> ()
      simulation.call @ascending(%ctx, %out) : (!simulation.context, !simulation.ref<!counter>) -> ()
      simulation.call @zero_trip(%ctx, %out) : (!simulation.context, !simulation.ref<!counter>) -> ()
      simulation.call @unsigned_direct(%ctx, %out) : (!simulation.context, !simulation.ref<!counter>) -> ()
      %array = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %neighbor = simulation.ref.subelement %array[[0]] : !simulation.ref<!array> -> !simulation.ref<!element>
      %x = simulation.logic.constant 0 : i79, -1 : i79 : !element
      simulation.ref.store %x to %neighbor : !element, !simulation.ref<!element>
      simulation.call @packed_copy(%ctx, %array) : (!simulation.context, !simulation.ref<!array>) -> ()
      simulation.call @observe_element(%ctx, %neighbor, %x) : (!simulation.context, !simulation.ref<!element>, !element) -> ()
      simulation.return
    }
  }
}
