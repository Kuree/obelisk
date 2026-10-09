// RUN: obelisk-opt %s --canonicalize | FileCheck %s

// LRM 7.4.5: "Writing to an array with an invalid index shall perform no
// operation". Static views require a known in-range source index. Their
// ordinal follows declared bounds, including descending and negative ranges.
// Unknown and out-of-range reference indices retain checked runtime accesses.
module {
  simulation.design @indices {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "ascending_first"
    // CHECK-LABEL: simulation.func @ascending_first(
    // CHECK: simulation.ref.subelement %{{.*}}[0]
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.return
    simulation.func @ascending_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<2 : 5 x i8>> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %index = arith.constant 2 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<2 : 5 x i8>>, i65) -> !simulation.ref<i8>
      %v = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 2 in 0 function hierarchy "ascending_last"
    // CHECK-LABEL: simulation.func @ascending_last(
    // CHECK: simulation.ref.subelement %{{.*}}[3]
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.return
    simulation.func @ascending_last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<2 : 5 x i8>> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %index = arith.constant 5 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<2 : 5 x i8>>, i65) -> !simulation.ref<i8>
      %v = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 3 in 0 function hierarchy "descending_first"
    // CHECK-LABEL: simulation.func @descending_first(
    // CHECK: simulation.ref.subelement %{{.*}}[0]
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.return
    simulation.func @descending_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<5 : 2 x i8>> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %index = arith.constant 5 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<5 : 2 x i8>>, i65) -> !simulation.ref<i8>
      %v = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 4 in 0 function hierarchy "descending_last"
    // CHECK-LABEL: simulation.func @descending_last(
    // CHECK: simulation.ref.subelement %{{.*}}[3]
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.return
    simulation.func @descending_last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<5 : 2 x i8>> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %index = arith.constant 2 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<5 : 2 x i8>>, i65) -> !simulation.ref<i8>
      %v = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 5 in 0 function hierarchy "negative_ascending"
    // CHECK-LABEL: simulation.func @negative_ascending(
    // CHECK: simulation.ref.subelement %{{.*}}[1]
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.return
    simulation.func @negative_ascending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<-5 : -2 x i8>> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %index = arith.constant -4 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<-5 : -2 x i8>>, i65) -> !simulation.ref<i8>
      %v = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 6 in 0 function hierarchy "negative_descending"
    // CHECK-LABEL: simulation.func @negative_descending(
    // CHECK: simulation.ref.subelement %{{.*}}[2]
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.return
    simulation.func @negative_descending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<-2 : -5 x i8>> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %index = arith.constant -4 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<-2 : -5 x i8>>, i65) -> !simulation.ref<i8>
      %v = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 7 in 0 function hierarchy "packed_ascending"
    // CHECK-LABEL: simulation.func @packed_ascending(
    // CHECK: simulation.ref.subelement %{{.*}}[0]
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.return
    simulation.func @packed_ascending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.packed_array<2 : 5 x i8>> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %index = arith.constant 2 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<2 : 5 x i8>>, i65) -> !simulation.ref<i8>
      %v = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 8 in 0 function hierarchy "packed_descending"
    // CHECK-LABEL: simulation.func @packed_descending(
    // CHECK: simulation.ref.subelement %{{.*}}[3]
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.return
    simulation.func @packed_descending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.packed_array<5 : 2 x i8>> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %index = arith.constant 2 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.packed_array<5 : 2 x i8>>, i65) -> !simulation.ref<i8>
      %v = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 9 in 0 function hierarchy "singleton"
    // CHECK-LABEL: simulation.func @singleton(
    // CHECK: simulation.ref.subelement %{{.*}}[0]
    // CHECK-NOT: simulation.ref.array_element
    // CHECK: simulation.return
    simulation.func @singleton(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<-3 : -3 x i8>> {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %index = arith.constant -3 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<-3 : -3 x i8>>, i65) -> !simulation.ref<i8>
      %v = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.return %v : i8
    }
    simulation.code_unit.decl 10 in 0 function hierarchy "known_logic"
    // CHECK-LABEL: simulation.func @known_logic(
    // CHECK: simulation.ref.subelement
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @known_logic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<2 : 5 x i8>> {simulation.capture_kind = 1 : i32}, %v: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %index = simulation.logic.constant 3 : i65, 0 : i65 : !simulation.logic<65>
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<2 : 5 x i8>>, !simulation.logic<65>) -> !simulation.ref<i8>
      %r = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %element : i8, !simulation.ref<i8>
      simulation.return %r : i8
    }
    simulation.code_unit.decl 11 in 0 function hierarchy "x_index"
    // CHECK-LABEL: simulation.func @x_index(
    // CHECK: simulation.ref.array_element
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @x_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<2 : 5 x i8>> {simulation.capture_kind = 1 : i32}, %v: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %index = simulation.logic.constant 0 : i65, 1 : i65 : !simulation.logic<65>
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<2 : 5 x i8>>, !simulation.logic<65>) -> !simulation.ref<i8>
      %r = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %element : i8, !simulation.ref<i8>
      simulation.return %r : i8
    }
    simulation.code_unit.decl 12 in 0 function hierarchy "z_index"
    // CHECK-LABEL: simulation.func @z_index(
    // CHECK: simulation.ref.array_element
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @z_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<2 : 5 x i8>> {simulation.capture_kind = 1 : i32}, %v: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %index = simulation.logic.constant 1 : i65, 1 : i65 : !simulation.logic<65>
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<2 : 5 x i8>>, !simulation.logic<65>) -> !simulation.ref<i8>
      %r = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %element : i8, !simulation.ref<i8>
      simulation.return %r : i8
    }
    simulation.code_unit.decl 13 in 0 function hierarchy "below"
    // CHECK-LABEL: simulation.func @below(
    // CHECK: simulation.ref.array_element
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @below(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<2 : 5 x i8>> {simulation.capture_kind = 1 : i32}, %v: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 13 : i64} {
      %index = arith.constant 1 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<2 : 5 x i8>>, i65) -> !simulation.ref<i8>
      %r = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %element : i8, !simulation.ref<i8>
      simulation.return %r : i8
    }
    simulation.code_unit.decl 14 in 0 function hierarchy "above"
    // CHECK-LABEL: simulation.func @above(
    // CHECK: simulation.ref.array_element
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @above(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<2 : 5 x i8>> {simulation.capture_kind = 1 : i32}, %v: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 14 : i64} {
      %index = arith.constant 6 : i65
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<2 : 5 x i8>>, i65) -> !simulation.ref<i8>
      %r = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %element : i8, !simulation.ref<i8>
      simulation.return %r : i8
    }
    simulation.code_unit.decl 15 in 0 function hierarchy "wide"
    // CHECK-LABEL: simulation.func @wide(
    // CHECK: simulation.ref.array_element
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @wide(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<!simulation.unpacked_array<2 : 5 x i8>> {simulation.capture_kind = 1 : i32}, %v: i8 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 15 : i64} {
      %index = arith.constant 18446744073709551619 : i128
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!simulation.unpacked_array<2 : 5 x i8>>, i128) -> !simulation.ref<i8>
      %r = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %element : i8, !simulation.ref<i8>
      simulation.return %r : i8
    }
    simulation.code_unit.decl 16 in 0 function hierarchy "value_ascending"
    // CHECK-LABEL: simulation.func @value_ascending(
    // CHECK: simulation.aggregate.extract {{.*}}[1]
    // CHECK: simulation.aggregate.insert
    // CHECK: simulation.return
    simulation.func @value_ascending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.unpacked_array<2 : 5 x !simulation.logic<8>> {simulation.capture_kind = 1 : i32}, %v: !simulation.logic<8> {simulation.capture_kind = 1 : i32}) -> (!simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>) attributes {entry_kind = 8 : i32, code_unit_id = 16 : i64} {
      %i = arith.constant 3 : i65
      %r = simulation.array.extract_dynamic %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, i65) -> !simulation.logic<8>
      %w = simulation.array.insert_dynamic %v into %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, !simulation.logic<8>, i65) -> !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
      simulation.return %r, %w : !simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
    }
    simulation.code_unit.decl 17 in 0 function hierarchy "value_below"
    // CHECK-LABEL: simulation.func @value_below(
    // CHECK: simulation.logic.constant 0 : i8, -1 : i8
    // CHECK-NOT: simulation.array.
    // CHECK-NOT: simulation.aggregate.insert
    // CHECK: simulation.return
    simulation.func @value_below(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.unpacked_array<2 : 5 x !simulation.logic<8>> {simulation.capture_kind = 1 : i32}, %v: !simulation.logic<8> {simulation.capture_kind = 1 : i32}) -> (!simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>) attributes {entry_kind = 8 : i32, code_unit_id = 17 : i64} {
      %i = arith.constant 1 : i65
      %r = simulation.array.extract_dynamic %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, i65) -> !simulation.logic<8>
      %w = simulation.array.insert_dynamic %v into %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, !simulation.logic<8>, i65) -> !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
      simulation.return %r, %w : !simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
    }
    simulation.code_unit.decl 18 in 0 function hierarchy "value_above"
    // CHECK-LABEL: simulation.func @value_above(
    // CHECK: simulation.logic.constant 0 : i8, -1 : i8
    // CHECK-NOT: simulation.array.
    // CHECK-NOT: simulation.aggregate.insert
    // CHECK: simulation.return
    simulation.func @value_above(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.unpacked_array<2 : 5 x !simulation.logic<8>> {simulation.capture_kind = 1 : i32}, %v: !simulation.logic<8> {simulation.capture_kind = 1 : i32}) -> (!simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>) attributes {entry_kind = 8 : i32, code_unit_id = 18 : i64} {
      %i = arith.constant 6 : i65
      %r = simulation.array.extract_dynamic %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, i65) -> !simulation.logic<8>
      %w = simulation.array.insert_dynamic %v into %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, !simulation.logic<8>, i65) -> !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
      simulation.return %r, %w : !simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
    }
    simulation.code_unit.decl 19 in 0 function hierarchy "value_x"
    // CHECK-LABEL: simulation.func @value_x(
    // CHECK: simulation.logic.constant 0 : i8, -1 : i8
    // CHECK-NOT: simulation.array.
    // CHECK-NOT: simulation.aggregate.insert
    // CHECK: simulation.return
    simulation.func @value_x(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.unpacked_array<2 : 5 x !simulation.logic<8>> {simulation.capture_kind = 1 : i32}, %v: !simulation.logic<8> {simulation.capture_kind = 1 : i32}) -> (!simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>) attributes {entry_kind = 8 : i32, code_unit_id = 19 : i64} {
      %i = simulation.logic.constant 0 : i65, 1 : i65 : !simulation.logic<65>
      %r = simulation.array.extract_dynamic %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, !simulation.logic<65>) -> !simulation.logic<8>
      %w = simulation.array.insert_dynamic %v into %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, !simulation.logic<8>, !simulation.logic<65>) -> !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
      simulation.return %r, %w : !simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
    }
    simulation.code_unit.decl 20 in 0 function hierarchy "value_z"
    // CHECK-LABEL: simulation.func @value_z(
    // CHECK: simulation.logic.constant 0 : i8, -1 : i8
    // CHECK-NOT: simulation.array.
    // CHECK-NOT: simulation.aggregate.insert
    // CHECK: simulation.return
    simulation.func @value_z(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.unpacked_array<2 : 5 x !simulation.logic<8>> {simulation.capture_kind = 1 : i32}, %v: !simulation.logic<8> {simulation.capture_kind = 1 : i32}) -> (!simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>) attributes {entry_kind = 8 : i32, code_unit_id = 20 : i64} {
      %i = simulation.logic.constant 1 : i65, 1 : i65 : !simulation.logic<65>
      %r = simulation.array.extract_dynamic %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, !simulation.logic<65>) -> !simulation.logic<8>
      %w = simulation.array.insert_dynamic %v into %a[%i] : (!simulation.unpacked_array<2 : 5 x !simulation.logic<8>>, !simulation.logic<8>, !simulation.logic<65>) -> !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
      simulation.return %r, %w : !simulation.logic<8>, !simulation.unpacked_array<2 : 5 x !simulation.logic<8>>
    }
  }
}
