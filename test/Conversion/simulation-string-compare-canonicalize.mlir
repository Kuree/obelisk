// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module {
  // This is the Simulation IR shape produced for a string selector tested
  // against a static unpacked-array item in an `inside` expression. Aggregate
  // extraction and string comparison are intentionally independent folds.
  func.func @fold_static_array_inside() -> i1 {
    %selector = obelisk_sim.string.literal "RC"
    %ro = obelisk_sim.string.literal "RO"
    %rc = obelisk_sim.string.literal "RC"
    %items = obelisk_sim.aggregate.construct %ro, %rc :
      (!obelisk_sim.string, !obelisk_sim.string) ->
      !obelisk_sim.unpacked_array<1 : 0 x !obelisk_sim.string>
    %first = obelisk_sim.aggregate.extract %items[0] :
      (!obelisk_sim.unpacked_array<1 : 0 x !obelisk_sim.string>) ->
      !obelisk_sim.string
    %second = obelisk_sim.aggregate.extract %items[1] :
      (!obelisk_sim.unpacked_array<1 : 0 x !obelisk_sim.string>) ->
      !obelisk_sim.string
    %zero = arith.constant 0 : i32
    %false = arith.constant false
    %first_cmp = obelisk_sim.string.compare %selector, %first
      case_insensitive = false
    %first_eq = arith.cmpi eq, %first_cmp, %zero : i32
    %first_match = arith.ori %false, %first_eq : i1
    %second_cmp = obelisk_sim.string.compare %selector, %second
      case_insensitive = false
    %second_eq = arith.cmpi eq, %second_cmp, %zero : i32
    %matched = arith.ori %first_match, %second_eq : i1
    return %matched : i1
  }

  func.func @fold_literal_comparisons() -> (i32, i32, i32, i32, i32) {
    %upper = obelisk_sim.string.literal "Alpha"
    %lower = obelisk_sim.string.literal "alpha"
    %sensitive = obelisk_sim.string.compare %upper, %lower
      case_insensitive = false
    %insensitive = obelisk_sim.string.compare %upper, %lower
      case_insensitive = true
    %short = obelisk_sim.string.literal "ab"
    %long = obelisk_sim.string.literal "abc"
    %prefix = obelisk_sim.string.compare %short, %long
      case_insensitive = false
    %b = obelisk_sim.string.literal "b"
    %a = obelisk_sim.string.literal "a"
    %greater = obelisk_sim.string.compare %b, %a
      case_insensitive = false
    %punctuation_left = obelisk_sim.string.literal "A["
    %punctuation_right = obelisk_sim.string.literal "a{"
    %ascii_only = obelisk_sim.string.compare %punctuation_left, %punctuation_right
      case_insensitive = true
    return %sensitive, %insensitive, %prefix, %greater, %ascii_only :
      i32, i32, i32, i32, i32
  }

  func.func @preserve_dynamic_compare(%lhs: !obelisk_sim.string,
                                      %rhs: !obelisk_sim.string) -> i32 {
    %comparison = obelisk_sim.string.compare %lhs, %rhs
      case_insensitive = false
    return %comparison : i32
  }
}

// CHECK-LABEL: func.func @fold_static_array_inside
// CHECK: %[[TRUE:.*]] = arith.constant true
// CHECK-NOT: obelisk_sim.aggregate.
// CHECK-NOT: obelisk_sim.string.compare
// CHECK-NOT: arith.cmpi
// CHECK-NOT: arith.ori
// CHECK: return %[[TRUE]] : i1
// CHECK-LABEL: func.func @fold_literal_comparisons
// CHECK-NOT: obelisk_sim.string.compare
// CHECK-DAG: %[[LESS:.*]] = arith.constant -1 : i32
// CHECK-DAG: %[[EQUAL:.*]] = arith.constant 0 : i32
// CHECK-DAG: %[[GREATER:.*]] = arith.constant 1 : i32
// CHECK: return %[[LESS]], %[[EQUAL]], %[[LESS]], %[[GREATER]], %[[LESS]]
// CHECK-LABEL: func.func @preserve_dynamic_compare
// CHECK: obelisk_sim.string.compare %{{.*}}, %{{.*}} case_insensitive = false
