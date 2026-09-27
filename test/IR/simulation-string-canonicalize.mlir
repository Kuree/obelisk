// RUN: obelisk-opt %s -canonicalize | FileCheck %s

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
