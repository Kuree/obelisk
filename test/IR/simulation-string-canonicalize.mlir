// RUN: obelisk-opt %s -canonicalize | FileCheck %s

module {
  func.func @literal_length() -> i64 {
    %literal = obelisk_sim.string.literal "ab\00c"
    %length = obelisk_sim.string.length %literal :
      (!obelisk_sim.string) -> i64
    return %length : i64
  }
}

// CHECK-LABEL: func.func @literal_length
// CHECK: %[[LENGTH:.*]] = arith.constant 4 : i64
// CHECK-NOT: obelisk_sim.string.length
// CHECK: return %[[LENGTH]] : i64
