// RUN: obelisk-opt %s --canonicalize | FileCheck %s

// Without any simulation type the arith dialect is only loaded because the
// schedule dialect depends on it; folding a known integer predicate creates
// an arith constant.
// CHECK-LABEL: func.func @integer
// CHECK-NEXT: %[[TRUE:.*]] = arith.constant true
// CHECK-NEXT: return %[[TRUE]]
func.func @integer(%bits: i8) -> i1 {
  %known = schedule.known.value %bits : (i8) -> i1
  return %known : i1
}
