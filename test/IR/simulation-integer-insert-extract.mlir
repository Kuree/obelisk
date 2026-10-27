// RUN: talos-opt %s --canonicalize | FileCheck %s
// RUN: talos-opt %s --canonicalize='top-down=false' | FileCheck %s
// RUN: talos-opt %s --canonicalize -o %t.once
// RUN: talos-opt %t.once --canonicalize -o %t.twice
// RUN: diff %t.once %t.twice

// Integer selections retain the extract-of-insert simplifications available
// to four-state logic. IEEE 1800-2023 11.5.1 preserves only in-range writes;
// the rules below must not assume an entire replacement survived clipping.

// CHECK-LABEL: func.func @low(
// CHECK-NEXT: return %arg1 : i4
func.func @low(%old: i8, %new: i4) -> i4 {
  %zero = arith.constant 0 : i64
  %updated = simulation.bits.dyn_insert %new into %old at %zero : (i8, i4, i64) -> i8
  %part = arith.trunci %updated : i8 to i4
  return %part : i4
}

// CHECK-LABEL: func.func @shifted(
// CHECK-NEXT: return %arg1 : i4
func.func @shifted(%old: i8, %new: i4) -> i4 {
  %index = arith.constant 2 : i128
  %two = arith.constant 2 : i8
  %updated = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i128) -> i8
  %shifted = arith.shrui %updated, %two : i8
  %part = arith.trunci %shifted : i8 to i4
  return %part : i4
}

// CHECK-LABEL: func.func @contained(
// CHECK-NEXT: %[[ONE:.*]] = arith.constant 1 : i4
// CHECK-NEXT: %[[SHIFT:.*]] = arith.shrui %arg1, %[[ONE]] : i4
// CHECK-NEXT: %[[PART:.*]] = arith.trunci %[[SHIFT]] : i4 to i2
// CHECK-NEXT: return %[[PART]] : i2
func.func @contained(%old: i8, %new: i4) -> i2 {
  %index = arith.constant 2 : i64
  %three = arith.constant 3 : i8
  %updated = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i64) -> i8
  %shifted = arith.shrui %updated, %three : i8
  %part = arith.trunci %shifted : i8 to i2
  return %part : i2
}

// CHECK-LABEL: func.func @disjoint(
// CHECK-NEXT: %[[PART:.*]] = arith.trunci %arg0 : i8 to i4
// CHECK-NEXT: return %[[PART]] : i4
func.func @disjoint(%old: i8, %new: i4) -> i4 {
  %index = arith.constant 4 : i64
  %updated = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i64) -> i8
  %part = arith.trunci %updated : i8 to i4
  return %part : i4
}

// The dialect extraction also folds; equal indices need not have equal widths.
// CHECK-LABEL: func.func @dialect_extract(
// CHECK-NEXT: return %arg1 : i4
func.func @dialect_extract(%old: i8, %new: i4) -> i4 {
  %write = arith.constant 2 : i64
  %read = arith.constant 2 : i128
  %updated = simulation.bits.dyn_insert %new into %old at %write : (i8, i4, i64) -> i8
  %part = simulation.bits.dyn_extract %updated from %read : (i8, i128) -> i4
  return %part : i4
}

// CHECK-LABEL: func.func @dialect_contained(
// CHECK-NEXT: %[[ONE:.*]] = arith.constant 1 : i4
// CHECK-NEXT: %[[SHIFT:.*]] = arith.shrui %arg1, %[[ONE]] : i4
// CHECK-NEXT: %[[PART:.*]] = arith.trunci %[[SHIFT]] : i4 to i2
// CHECK-NEXT: return %[[PART]] : i2
func.func @dialect_contained(%old: i8, %new: i4) -> i2 {
  %write = arith.constant 2 : i64
  %read = arith.constant 3 : i64
  %updated = simulation.bits.dyn_insert %new into %old at %write : (i8, i4, i64) -> i8
  %part = simulation.bits.dyn_extract %updated from %read : (i8, i64) -> i2
  return %part : i2
}

// Other users keep the complete insertion, including its old high bits.
// CHECK-LABEL: func.func @other_user(
// CHECK: %[[INSERT:.*]] = simulation.bits.dyn_insert
// CHECK-NEXT: return %[[INSERT]], %arg1 : i8, i4
func.func @other_user(%old: i8, %new: i4) -> (i8, i4) {
  %zero = arith.constant 0 : i64
  %updated = simulation.bits.dyn_insert %new into %old at %zero : (i8, i4, i64) -> i8
  %part = arith.trunci %updated : i8 to i4
  return %updated, %part : i8, i4
}

// A selection crossing the insertion boundary still needs both operands.
// CHECK-LABEL: func.func @partial_overlap(
// CHECK: %[[INSERT:.*]] = simulation.bits.dyn_insert
// CHECK-NEXT: %[[PART:.*]] = arith.trunci %[[INSERT]] : i8 to i4
// CHECK-NEXT: return %[[PART]] : i4
func.func @partial_overlap(%old: i8, %new: i4) -> i4 {
  %two = arith.constant 2 : i64
  %updated = simulation.bits.dyn_insert %new into %old at %two : (i8, i4, i64) -> i8
  %part = arith.trunci %updated : i8 to i4
  return %part : i4
}

// CHECK-LABEL: func.func @dynamic_insert(
// CHECK: %[[INSERT:.*]] = simulation.bits.dyn_insert %arg1 into %arg0 at %arg2
// CHECK-NEXT: %[[PART:.*]] = arith.trunci %[[INSERT]] : i8 to i4
// CHECK-NEXT: return %[[PART]] : i4
func.func @dynamic_insert(%old: i8, %new: i4, %index: i64) -> i4 {
  %updated = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i64) -> i8
  %part = arith.trunci %updated : i8 to i4
  return %part : i4
}

// CHECK-LABEL: func.func @dynamic_extract(
// CHECK: %[[INSERT:.*]] = simulation.bits.dyn_insert
// CHECK-NEXT: %[[PART:.*]] = simulation.bits.dyn_extract %[[INSERT]] from %arg2
// CHECK-NEXT: return %[[PART]] : i4
func.func @dynamic_extract(%old: i8, %new: i4, %index: i64) -> i4 {
  %zero = arith.constant 0 : i64
  %updated = simulation.bits.dyn_insert %new into %old at %zero : (i8, i4, i64) -> i8
  %part = simulation.bits.dyn_extract %updated from %index : (i8, i64) -> i4
  return %part : i4
}

// Clipped writes followed by the matching read zero-fill clipped bits rather
// than returning the full replacement. Both sides of the bounds are covered.
// CHECK-LABEL: func.func @clipped_low(
// CHECK: %[[INSERT:.*]] = simulation.bits.dyn_insert
// CHECK-NEXT: %[[PART:.*]] = simulation.bits.dyn_extract %[[INSERT]]
// CHECK-NEXT: return %[[PART]] : i4
func.func @clipped_low(%old: i8, %new: i4) -> i4 {
  %index = arith.constant -1 : i128
  %updated = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i128) -> i8
  %part = simulation.bits.dyn_extract %updated from %index : (i8, i128) -> i4
  return %part : i4
}

// CHECK-LABEL: func.func @clipped_high(
// CHECK: %[[INSERT:.*]] = simulation.bits.dyn_insert
// CHECK-NEXT: %[[PART:.*]] = simulation.bits.dyn_extract %[[INSERT]]
// CHECK-NEXT: return %[[PART]] : i4
func.func @clipped_high(%old: i8, %new: i4) -> i4 {
  %index = arith.constant 6 : i128
  %updated = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i128) -> i8
  %part = simulation.bits.dyn_extract %updated from %index : (i8, i128) -> i4
  return %part : i4
}

// CHECK-LABEL: func.func @outside(
// CHECK-NEXT: %[[ZERO:.*]] = arith.constant 0 : i4
// CHECK-NEXT: return %[[ZERO]] : i4
func.func @outside(%old: i8, %new: i4) -> i4 {
  %index = arith.constant 18446744073709551616 : i128
  %updated = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i128) -> i8
  %part = simulation.bits.dyn_extract %updated from %index : (i8, i128) -> i4
  return %part : i4
}

// CHECK-LABEL: func.func @unknown_index(
// CHECK-NEXT: %[[ZERO:.*]] = arith.constant 0 : i4
// CHECK-NEXT: return %[[ZERO]] : i4
func.func @unknown_index(%old: i8, %new: i4) -> i4 {
  %index = simulation.logic.constant 0 : i4, 1 : i4 : !simulation.logic<4>
  %updated = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, !simulation.logic<4>) -> i8
  %part = simulation.bits.dyn_extract %updated from %index : (i8, !simulation.logic<4>) -> i4
  return %part : i4
}

// Overflow and exact flags observe discarded bits through their poison rules.
// CHECK-LABEL: func.func @flagged_truncation(
// CHECK: %[[INSERT:.*]] = simulation.bits.dyn_insert
// CHECK-NEXT: %[[PART:.*]] = arith.trunci %[[INSERT]] overflow<nsw, nuw> : i8 to i4
// CHECK-NEXT: return %[[PART]] : i4
func.func @flagged_truncation(%old: i8, %new: i4) -> i4 {
  %zero = arith.constant 0 : i64
  %updated = simulation.bits.dyn_insert %new into %old at %zero : (i8, i4, i64) -> i8
  %part = arith.trunci %updated overflow<nsw, nuw> : i8 to i4
  return %part : i4
}

// CHECK-LABEL: func.func @exact_shift(
// CHECK: %[[INSERT:.*]] = simulation.bits.dyn_insert
// CHECK-NEXT: %[[SHIFT:.*]] = arith.shrui %[[INSERT]], %{{.*}} exact : i8
// CHECK-NEXT: %[[PART:.*]] = arith.trunci %[[SHIFT]] : i8 to i4
// CHECK-NEXT: return %[[PART]] : i4
func.func @exact_shift(%old: i8, %new: i4) -> i4 {
  %index = arith.constant 2 : i64
  %two = arith.constant 2 : i8
  %updated = simulation.bits.dyn_insert %new into %old at %index : (i8, i4, i64) -> i8
  %shifted = arith.shrui %updated, %two exact : i8
  %part = arith.trunci %shifted : i8 to i4
  return %part : i4
}
