// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module {
  simulation.design @folds {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.folds.round_trip.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.folds.same_width_resize.9000002"
    simulation.code_unit.decl 9000003 in 0 initial hierarchy "test.folds.time_math.9000003"
    simulation.code_unit.decl 9000004 in 0 initial hierarchy "test.folds.constants.9000004"
    simulation.code_unit.decl 9000005 in 0 function hierarchy "test.folds.to_bits_matrix.9000005"
    simulation.code_unit.decl 9000006 in 0 function hierarchy "test.folds.truth_matrix.9000006"
    simulation.code_unit.decl 9000007 in 0 function hierarchy "test.folds.retyping_ref_extract.9000007"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : i32 design

    // to_bits(from_bits(x)) is the identity; the reverse is not, because
    // from_bits cannot carry an unknown plane.
    // CHECK-LABEL: simulation.func @round_trip
    // CHECK-NEXT: simulation.return %arg1
    simulation.func @round_trip(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %bits: i8 {simulation.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %logic = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %back = simulation.logic.to_bits %logic : !simulation.logic<8> -> i8
      simulation.return %back : i8
    }

    // A resize to the same width is a no-op regardless of signedness.
    // CHECK-LABEL: simulation.func @same_width_resize
    // CHECK-NOT: simulation.logic.resize
    simulation.func @same_width_resize(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %resized = simulation.logic.resize %value signed = true : !simulation.logic<8> -> !simulation.logic<8>
      simulation.return %resized : !simulation.logic<8>
    }

    // Constant time folds and rematerializes through the dialect constant
    // materializer; adding zero disappears entirely.
    // CHECK-LABEL: simulation.func @time_math
    // CHECK: %[[T:.*]] = simulation.time.constant 9
    // CHECK: simulation.suspend.delay %[[T]]
    simulation.func @time_math(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %four = simulation.time.constant 4
      %five = simulation.time.constant 5
      %zero = simulation.time.constant 0
      %sum = simulation.time.add %four, %five
      %same = simulation.time.add %sum, %zero
      simulation.suspend.delay %same to ^next
    ^next:
      simulation.return
    }

    // Identical four-state constants are common subexpressions.
    // CHECK-LABEL: simulation.func @constants
    // CHECK: simulation.logic.constant
    // CHECK-NOT: simulation.logic.constant
    simulation.func @constants(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %a = simulation.logic.constant 3 : i8, 0 : i8 : !simulation.logic<8>
      %b = simulation.logic.constant 3 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %a to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %b to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    // Four-state to two-state conversion clears every unknown position,
    // including Z positions whose value-plane bit is one.
    // CHECK-LABEL: simulation.func @to_bits_matrix
    // CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i4
    // CHECK-DAG: %[[ONE:.*]] = arith.constant 1 : i4
    // CHECK-DAG: %[[FIVE:.*]] = arith.constant 5 : i4
    // CHECK: simulation.return %[[ZERO]], %[[ONE]], %[[ZERO]], %[[ZERO]], %[[FIVE]]
    simulation.func @to_bits_matrix(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (i4, i4, i4, i4, i4) attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %zero = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      %one = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
      %x = simulation.logic.constant 0 : i4, 15 : i4 : !simulation.logic<4>
      %z = simulation.logic.constant 15 : i4, 15 : i4 : !simulation.logic<4>
      %mixed = simulation.logic.constant 13 : i4, 10 : i4 : !simulation.logic<4>
      %zero_bits = simulation.logic.to_bits %zero : !simulation.logic<4> -> i4
      %one_bits = simulation.logic.to_bits %one : !simulation.logic<4> -> i4
      %x_bits = simulation.logic.to_bits %x : !simulation.logic<4> -> i4
      %z_bits = simulation.logic.to_bits %z : !simulation.logic<4> -> i4
      %mixed_bits = simulation.logic.to_bits %mixed : !simulation.logic<4> -> i4
      simulation.return %zero_bits, %one_bits, %x_bits, %z_bits, %mixed_bits : i4, i4, i4, i4, i4
    }

    // Truth is true for any known one, but not for zero, X-only, Z-only, or
    // mixtures whose only one-valued positions are unknown.
    // CHECK-LABEL: simulation.func @truth_matrix
    // CHECK-DAG: %[[FALSE:.*]] = arith.constant false
    // CHECK-DAG: %[[TRUE:.*]] = arith.constant true
    // CHECK: simulation.return %[[FALSE]], %[[TRUE]], %[[FALSE]], %[[FALSE]], %[[TRUE]], %[[FALSE]]
    simulation.func @truth_matrix(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (i1, i1, i1, i1, i1, i1) attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %zero = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      %one = simulation.logic.constant 1 : i4, 0 : i4 : !simulation.logic<4>
      %x = simulation.logic.constant 0 : i4, 15 : i4 : !simulation.logic<4>
      %z = simulation.logic.constant 15 : i4, 15 : i4 : !simulation.logic<4>
      %mixed_true = simulation.logic.constant 13 : i4, 10 : i4 : !simulation.logic<4>
      %mixed_false = simulation.logic.constant 10 : i4, 10 : i4 : !simulation.logic<4>
      %zero_truth = simulation.logic.is_true %zero : !simulation.logic<4>
      %one_truth = simulation.logic.is_true %one : !simulation.logic<4>
      %x_truth = simulation.logic.is_true %x : !simulation.logic<4>
      %z_truth = simulation.logic.is_true %z : !simulation.logic<4>
      %mixed_true_truth = simulation.logic.is_true %mixed_true : !simulation.logic<4>
      %mixed_false_truth = simulation.logic.is_true %mixed_false : !simulation.logic<4>
      simulation.return %zero_truth, %one_truth, %x_truth, %z_truth, %mixed_true_truth, %mixed_false_truth : i1, i1, i1, i1, i1, i1
    }

    // A full-width ref.extract that retypes its element -- `int` viewed as the
    // `bit [31:0]` an IEEE 1800-2017 11.5.1 part-select of it produces -- is
    // not the identity, so it survives. Folding it away would leave the store
    // below writing a packed array through a reference to `i32`.
    // CHECK-LABEL: simulation.func @retyping_ref_extract
    // CHECK: %[[VIEW:.*]] = simulation.ref.extract %arg1 from 0 : !simulation.ref<i32> -> !simulation.ref<!simulation.packed_array<31 : 0 x i1>>
    // CHECK: simulation.ref.store %arg2 to %[[VIEW]]
    simulation.func @retyping_ref_extract(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %value: !simulation.packed_array<31 : 0 x i1> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9000007 : i64} {
      %view = simulation.ref.extract %ref from 0 : !simulation.ref<i32> -> !simulation.ref<!simulation.packed_array<31 : 0 x i1>>
      simulation.ref.store %value to %view : !simulation.packed_array<31 : 0 x i1>, !simulation.ref<!simulation.packed_array<31 : 0 x i1>>
      simulation.return
    }
  }
}
