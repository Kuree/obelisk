// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module {
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

// CHECK-LABEL: func.func @fold_literal_comparisons
// CHECK-NOT: obelisk_sim.string.compare
// CHECK-DAG: %[[LESS:.*]] = arith.constant -1 : i32
// CHECK-DAG: %[[EQUAL:.*]] = arith.constant 0 : i32
// CHECK-DAG: %[[GREATER:.*]] = arith.constant 1 : i32
// CHECK: return %[[LESS]], %[[EQUAL]], %[[LESS]], %[[GREATER]], %[[LESS]]
// CHECK-LABEL: func.func @preserve_dynamic_compare
// CHECK: obelisk_sim.string.compare %{{.*}}, %{{.*}} case_insensitive = false
