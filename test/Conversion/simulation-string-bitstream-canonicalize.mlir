// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module {
  func.func @exact_literal() -> i24 {
    %text = simulation.string.literal "ABC"
    %packed, %matched = simulation.string.to_packed_exact %text :
        (!simulation.string) -> (i24, i1)
    cf.assert %matched, "exact literal must match"
    return %packed : i24
  }

  func.func @ordinary_literal() -> i24 {
    %text = simulation.string.literal "ABC"
    %packed = simulation.string.to_packed %text :
        (!simulation.string) -> i24
    return %packed : i24
  }
}

// CHECK-LABEL: func.func @exact_literal
// CHECK: %[[ABC:.*]] = arith.constant 4276803 : i24
// CHECK: return %[[ABC]] : i24
// CHECK-LABEL: func.func @ordinary_literal
// CHECK: simulation.string.to_packed
