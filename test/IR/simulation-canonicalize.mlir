// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module {
  obelisk_sim.design @folds {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.folds.round_trip.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.folds.same_width_resize.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 initial hierarchy "test.folds.time_math.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 initial hierarchy "test.folds.constants.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 function hierarchy "test.folds.to_bits_matrix.9000005"
    obelisk_sim.code_unit.decl 9000006 in 0 function hierarchy "test.folds.truth_matrix.9000006"
    obelisk_sim.code_unit.decl 9000007 in 0 function hierarchy "test.folds.retyping_ref_extract.9000007"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 1 in 0 : i32 design

    // to_bits(from_bits(x)) is the identity; the reverse is not, because
    // from_bits cannot carry an unknown plane.
    // CHECK-LABEL: obelisk_sim.func @round_trip
    // CHECK-NEXT: obelisk_sim.return %arg1
    obelisk_sim.func @round_trip(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %bits: i8 {obelisk_sim.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %logic = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %back = obelisk_sim.logic.to_bits %logic : !obelisk_sim.logic<8> -> i8
      obelisk_sim.return %back : i8
    }

    // A resize to the same width is a no-op regardless of signedness.
    // CHECK-LABEL: obelisk_sim.func @same_width_resize
    // CHECK-NOT: obelisk_sim.logic.resize
    obelisk_sim.func @same_width_resize(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %resized = obelisk_sim.logic.resize %value signed = true : !obelisk_sim.logic<8> -> !obelisk_sim.logic<8>
      obelisk_sim.return %resized : !obelisk_sim.logic<8>
    }

    // Constant time folds and rematerializes through the dialect constant
    // materializer; adding zero disappears entirely.
    // CHECK-LABEL: obelisk_sim.func @time_math
    // CHECK: %[[T:.*]] = obelisk_sim.time.constant 9
    // CHECK: obelisk_sim.suspend.delay %[[T]]
    obelisk_sim.func @time_math(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %four = obelisk_sim.time.constant 4
      %five = obelisk_sim.time.constant 5
      %zero = obelisk_sim.time.constant 0
      %sum = obelisk_sim.time.add %four, %five
      %same = obelisk_sim.time.add %sum, %zero
      obelisk_sim.suspend.delay %same to ^next
    ^next:
      obelisk_sim.return
    }

    // Identical four-state constants are common subexpressions.
    // CHECK-LABEL: obelisk_sim.func @constants
    // CHECK: obelisk_sim.logic.constant
    // CHECK-NOT: obelisk_sim.logic.constant
    obelisk_sim.func @constants(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %a = obelisk_sim.logic.constant 3 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %b = obelisk_sim.logic.constant 3 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %a to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %b to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }

    // Four-state to two-state conversion clears every unknown position,
    // including Z positions whose value-plane bit is one.
    // CHECK-LABEL: obelisk_sim.func @to_bits_matrix
    // CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i4
    // CHECK-DAG: %[[ONE:.*]] = arith.constant 1 : i4
    // CHECK-DAG: %[[FIVE:.*]] = arith.constant 5 : i4
    // CHECK: obelisk_sim.return %[[ZERO]], %[[ONE]], %[[ZERO]], %[[ZERO]], %[[FIVE]]
    obelisk_sim.func @to_bits_matrix(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> (i4, i4, i4, i4, i4) attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %zero = obelisk_sim.logic.constant 0 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %one = obelisk_sim.logic.constant 1 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %x = obelisk_sim.logic.constant 0 : i4, 15 : i4 : !obelisk_sim.logic<4>
      %z = obelisk_sim.logic.constant 15 : i4, 15 : i4 : !obelisk_sim.logic<4>
      %mixed = obelisk_sim.logic.constant 13 : i4, 10 : i4 : !obelisk_sim.logic<4>
      %zero_bits = obelisk_sim.logic.to_bits %zero : !obelisk_sim.logic<4> -> i4
      %one_bits = obelisk_sim.logic.to_bits %one : !obelisk_sim.logic<4> -> i4
      %x_bits = obelisk_sim.logic.to_bits %x : !obelisk_sim.logic<4> -> i4
      %z_bits = obelisk_sim.logic.to_bits %z : !obelisk_sim.logic<4> -> i4
      %mixed_bits = obelisk_sim.logic.to_bits %mixed : !obelisk_sim.logic<4> -> i4
      obelisk_sim.return %zero_bits, %one_bits, %x_bits, %z_bits, %mixed_bits : i4, i4, i4, i4, i4
    }

    // Truth is true for any known one, but not for zero, X-only, Z-only, or
    // mixtures whose only one-valued positions are unknown.
    // CHECK-LABEL: obelisk_sim.func @truth_matrix
    // CHECK-DAG: %[[FALSE:.*]] = arith.constant false
    // CHECK-DAG: %[[TRUE:.*]] = arith.constant true
    // CHECK: obelisk_sim.return %[[FALSE]], %[[TRUE]], %[[FALSE]], %[[FALSE]], %[[TRUE]], %[[FALSE]]
    obelisk_sim.func @truth_matrix(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> (i1, i1, i1, i1, i1, i1) attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %zero = obelisk_sim.logic.constant 0 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %one = obelisk_sim.logic.constant 1 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %x = obelisk_sim.logic.constant 0 : i4, 15 : i4 : !obelisk_sim.logic<4>
      %z = obelisk_sim.logic.constant 15 : i4, 15 : i4 : !obelisk_sim.logic<4>
      %mixed_true = obelisk_sim.logic.constant 13 : i4, 10 : i4 : !obelisk_sim.logic<4>
      %mixed_false = obelisk_sim.logic.constant 10 : i4, 10 : i4 : !obelisk_sim.logic<4>
      %zero_truth = obelisk_sim.logic.is_true %zero : !obelisk_sim.logic<4>
      %one_truth = obelisk_sim.logic.is_true %one : !obelisk_sim.logic<4>
      %x_truth = obelisk_sim.logic.is_true %x : !obelisk_sim.logic<4>
      %z_truth = obelisk_sim.logic.is_true %z : !obelisk_sim.logic<4>
      %mixed_true_truth = obelisk_sim.logic.is_true %mixed_true : !obelisk_sim.logic<4>
      %mixed_false_truth = obelisk_sim.logic.is_true %mixed_false : !obelisk_sim.logic<4>
      obelisk_sim.return %zero_truth, %one_truth, %x_truth, %z_truth, %mixed_true_truth, %mixed_false_truth : i1, i1, i1, i1, i1, i1
    }

    // A full-width ref.extract that retypes its element -- `int` viewed as the
    // `bit [31:0]` an IEEE 1800-2017 11.5.1 part-select of it produces -- is
    // not the identity, so it survives. Folding it away would leave the store
    // below writing a packed array through a reference to `i32`.
    // CHECK-LABEL: obelisk_sim.func @retyping_ref_extract
    // CHECK: %[[VIEW:.*]] = obelisk_sim.ref.extract %arg1 from 0 : !obelisk_sim.ref<i32> -> !obelisk_sim.ref<!obelisk_sim.packed_array<31 : 0 x i1>>
    // CHECK: obelisk_sim.ref.store %arg2 to %[[VIEW]]
    obelisk_sim.func @retyping_ref_extract(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %value: !obelisk_sim.packed_array<31 : 0 x i1> {obelisk_sim.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9000007 : i64} {
      %view = obelisk_sim.ref.extract %ref from 0 : !obelisk_sim.ref<i32> -> !obelisk_sim.ref<!obelisk_sim.packed_array<31 : 0 x i1>>
      obelisk_sim.ref.store %value to %view : !obelisk_sim.packed_array<31 : 0 x i1>, !obelisk_sim.ref<!obelisk_sim.packed_array<31 : 0 x i1>>
      obelisk_sim.return
    }
  }
}
