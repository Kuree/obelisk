// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module {
  // This is the Simulation IR shape produced for a string selector tested
  // against a static unpacked-array item in an `inside` expression. Aggregate
  // extraction and string comparison are intentionally independent folds.
  func.func @fold_static_array_inside() -> i1 {
    %selector = simulation.string.literal "RC"
    %ro = simulation.string.literal "RO"
    %rc = simulation.string.literal "RC"
    %items = simulation.aggregate.construct %ro, %rc :
      (!simulation.string, !simulation.string) ->
      !simulation.unpacked_array<1 : 0 x !simulation.string>
    %first = simulation.aggregate.extract %items[0] :
      (!simulation.unpacked_array<1 : 0 x !simulation.string>) ->
      !simulation.string
    %second = simulation.aggregate.extract %items[1] :
      (!simulation.unpacked_array<1 : 0 x !simulation.string>) ->
      !simulation.string
    %zero = arith.constant 0 : i32
    %false = arith.constant false
    %first_cmp = simulation.string.compare %selector, %first
      case_insensitive = false
    %first_eq = arith.cmpi eq, %first_cmp, %zero : i32
    %first_match = arith.ori %false, %first_eq : i1
    %second_cmp = simulation.string.compare %selector, %second
      case_insensitive = false
    %second_eq = arith.cmpi eq, %second_cmp, %zero : i32
    %matched = arith.ori %first_match, %second_eq : i1
    return %matched : i1
  }

  func.func @fold_static_array_inside_miss() -> i1 {
    %selector = simulation.string.literal "RW"
    %ro = simulation.string.literal "RO"
    %rc = simulation.string.literal "RC"
    %items = simulation.aggregate.construct %ro, %rc :
      (!simulation.string, !simulation.string) ->
      !simulation.unpacked_array<1 : 0 x !simulation.string>
    %first = simulation.aggregate.extract %items[0] :
      (!simulation.unpacked_array<1 : 0 x !simulation.string>) ->
      !simulation.string
    %second = simulation.aggregate.extract %items[1] :
      (!simulation.unpacked_array<1 : 0 x !simulation.string>) ->
      !simulation.string
    %zero = arith.constant 0 : i32
    %false = arith.constant false
    %first_cmp = simulation.string.compare %selector, %first
      case_insensitive = false
    %first_eq = arith.cmpi eq, %first_cmp, %zero : i32
    %first_match = arith.ori %false, %first_eq : i1
    %second_cmp = simulation.string.compare %selector, %second
      case_insensitive = false
    %second_eq = arith.cmpi eq, %second_cmp, %zero : i32
    %matched = arith.ori %first_match, %second_eq : i1
    return %matched : i1
  }

  func.func @fold_literal_comparisons() -> (i32, i32, i32, i32, i32) {
    %upper = simulation.string.literal "Alpha"
    %lower = simulation.string.literal "alpha"
    %sensitive = simulation.string.compare %upper, %lower
      case_insensitive = false
    %insensitive = simulation.string.compare %upper, %lower
      case_insensitive = true
    %short = simulation.string.literal "ab"
    %long = simulation.string.literal "abc"
    %prefix = simulation.string.compare %short, %long
      case_insensitive = false
    %b = simulation.string.literal "b"
    %a = simulation.string.literal "a"
    %greater = simulation.string.compare %b, %a
      case_insensitive = false
    %punctuation_left = simulation.string.literal "A["
    %punctuation_right = simulation.string.literal "a{"
    %ascii_only = simulation.string.compare %punctuation_left, %punctuation_right
      case_insensitive = true
    return %sensitive, %insensitive, %prefix, %greater, %ascii_only :
      i32, i32, i32, i32, i32
  }

  func.func @preserve_dynamic_compare(%lhs: !simulation.string,
                                      %rhs: !simulation.string) -> i32 {
    %comparison = simulation.string.compare %lhs, %rhs
      case_insensitive = false
    return %comparison : i32
  }
}

// CHECK-LABEL: func.func @fold_static_array_inside
// CHECK: %[[TRUE:.*]] = arith.constant true
// CHECK-NOT: simulation.aggregate.
// CHECK-NOT: simulation.string.compare
// CHECK-NOT: arith.cmpi
// CHECK-NOT: arith.ori
// CHECK: return %[[TRUE]] : i1
// CHECK-LABEL: func.func @fold_static_array_inside_miss
// CHECK: %[[FALSE:.*]] = arith.constant false
// CHECK-NOT: simulation.aggregate.
// CHECK-NOT: simulation.string.compare
// CHECK-NOT: arith.cmpi
// CHECK-NOT: arith.ori
// CHECK: return %[[FALSE]] : i1
// CHECK-LABEL: func.func @fold_literal_comparisons
// CHECK-NOT: simulation.string.compare
// CHECK-DAG: %[[LESS:.*]] = arith.constant -1 : i32
// CHECK-DAG: %[[EQUAL:.*]] = arith.constant 0 : i32
// CHECK-DAG: %[[GREATER:.*]] = arith.constant 1 : i32
// CHECK: return %[[LESS]], %[[EQUAL]], %[[LESS]], %[[GREATER]], %[[LESS]]
// CHECK-LABEL: func.func @preserve_dynamic_compare
// CHECK: simulation.string.compare %{{.*}}, %{{.*}} case_insensitive = false
