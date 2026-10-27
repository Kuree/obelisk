// RUN: talos-opt %s -canonicalize | FileCheck %s

module {
  func.func @literal_length() -> i64 {
    %literal = simulation.string.literal "ab\00c"
    %length = simulation.string.length %literal :
      (!simulation.string) -> i64
    return %length : i64
  }
}

// CHECK-LABEL: func.func @literal_length
// CHECK: %[[LENGTH:.*]] = arith.constant 4 : i64
// CHECK-NOT: simulation.string.length
// CHECK: return %[[LENGTH]] : i64

// IEEE 1800-2023 6.16.7-8: either string comparison equals zero for itself.
// CHECK-LABEL: func.func @string_self
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK-NOT: simulation.string.compare
// CHECK: return %[[ZERO]], %[[ZERO]]
func.func @string_self(%s: !simulation.string) -> (i32, i32) {
  %a = simulation.string.compare %s, %s case_insensitive = false
  %b = simulation.string.compare %s, %s case_insensitive = true
  return %a, %b : i32, i32
}

// CHECK-LABEL: func.func @getc_0
// CHECK: %[[R:.*]] = arith.constant 65 : i8
// CHECK-NOT: simulation.string.getc
// CHECK: return %[[R]]
func.func @getc_0() -> i8 {
  %s = simulation.string.literal "A\FFB"
  %i = arith.constant 0 : i64
  %r = simulation.string.getc %s, %i : (!simulation.string, i64) -> i8
  return %r : i8
}

// CHECK-LABEL: func.func @getc_1
// CHECK: %[[R:.*]] = arith.constant -1 : i8
// CHECK-NOT: simulation.string.getc
// CHECK: return %[[R]]
func.func @getc_1() -> i8 {
  %s = simulation.string.literal "A\FFB"
  %i = arith.constant 1 : i64
  %r = simulation.string.getc %s, %i : (!simulation.string, i64) -> i8
  return %r : i8
}

// CHECK-LABEL: func.func @getc_2
// CHECK: %[[R:.*]] = arith.constant 66 : i8
// CHECK-NOT: simulation.string.getc
// CHECK: return %[[R]]
func.func @getc_2() -> i8 {
  %s = simulation.string.literal "A\FFB"
  %i = arith.constant 2 : i64
  %r = simulation.string.getc %s, %i : (!simulation.string, i64) -> i8
  return %r : i8
}

// CHECK-LABEL: func.func @getc_negative
// CHECK: %[[R:.*]] = arith.constant 0 : i8
// CHECK-NOT: simulation.string.getc
// CHECK: return %[[R]]
func.func @getc_negative() -> i8 {
  %s = simulation.string.literal "A\FFB"
  %i = arith.constant -1 : i64
  %r = simulation.string.getc %s, %i : (!simulation.string, i64) -> i8
  return %r : i8
}

// CHECK-LABEL: func.func @getc_3
// CHECK: %[[R:.*]] = arith.constant 0 : i8
// CHECK-NOT: simulation.string.getc
// CHECK: return %[[R]]
func.func @getc_3() -> i8 {
  %s = simulation.string.literal "A\FFB"
  %i = arith.constant 3 : i64
  %r = simulation.string.getc %s, %i : (!simulation.string, i64) -> i8
  return %r : i8
}

// CHECK-LABEL: func.func @getc_9223372036854775807
// CHECK: %[[R:.*]] = arith.constant 0 : i8
// CHECK-NOT: simulation.string.getc
// CHECK: return %[[R]]
func.func @getc_9223372036854775807() -> i8 {
  %s = simulation.string.literal "A\FFB"
  %i = arith.constant 9223372036854775807 : i64
  %r = simulation.string.getc %s, %i : (!simulation.string, i64) -> i8
  return %r : i8
}

// CHECK-LABEL: func.func @string_pack_1
// CHECK: %[[R:.*]] = arith.constant true
// CHECK-NOT: simulation.string.to_packed
// CHECK: return %[[R]]
func.func @string_pack_1() -> i1 {
  %s = simulation.string.literal "ABC"
  %r = simulation.string.to_packed %s : (!simulation.string) -> i1
  return %r : i1
}

// CHECK-LABEL: func.func @string_pack_7
// CHECK: %[[R:.*]] = arith.constant -61 : i7
// CHECK-NOT: simulation.string.to_packed
// CHECK: return %[[R]]
func.func @string_pack_7() -> i7 {
  %s = simulation.string.literal "ABC"
  %r = simulation.string.to_packed %s : (!simulation.string) -> i7
  return %r : i7
}

// CHECK-LABEL: func.func @string_pack_8
// CHECK: %[[R:.*]] = arith.constant 67 : i8
// CHECK-NOT: simulation.string.to_packed
// CHECK: return %[[R]]
func.func @string_pack_8() -> i8 {
  %s = simulation.string.literal "ABC"
  %r = simulation.string.to_packed %s : (!simulation.string) -> i8
  return %r : i8
}

// CHECK-LABEL: func.func @string_pack_12
// CHECK: %[[R:.*]] = arith.constant 579 : i12
// CHECK-NOT: simulation.string.to_packed
// CHECK: return %[[R]]
func.func @string_pack_12() -> i12 {
  %s = simulation.string.literal "ABC"
  %r = simulation.string.to_packed %s : (!simulation.string) -> i12
  return %r : i12
}

// CHECK-LABEL: func.func @string_pack_32
// CHECK: %[[R:.*]] = arith.constant 4276803 : i32
// CHECK-NOT: simulation.string.to_packed
// CHECK: return %[[R]]
func.func @string_pack_32() -> i32 {
  %s = simulation.string.literal "ABC"
  %r = simulation.string.to_packed %s : (!simulation.string) -> i32
  return %r : i32
}

// CHECK-LABEL: func.func @string_pack_65
// CHECK: %[[R:.*]] = arith.constant 4276803 : i65
// CHECK-NOT: simulation.string.to_packed
// CHECK: return %[[R]]
func.func @string_pack_65() -> i65 {
  %s = simulation.string.literal "ABC"
  %r = simulation.string.to_packed %s : (!simulation.string) -> i65
  return %r : i65
}

// CHECK-LABEL: func.func @string_pack_empty
// CHECK: arith.constant 0 : i65
// CHECK-NOT: simulation.string.to_packed
// CHECK: return
func.func @string_pack_empty() -> i65 {
  %s = simulation.string.literal ""
  %r = simulation.string.to_packed %s : (!simulation.string) -> i65
  return %r : i65
}
