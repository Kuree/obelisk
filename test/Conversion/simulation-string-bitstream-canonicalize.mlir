// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module {
  func.func @exact_literal() -> i24 {
    %text = obelisk_sim.string.literal "ABC"
    %packed, %matched = obelisk_sim.string.to_packed_exact %text :
        (!obelisk_sim.string) -> (i24, i1)
    cf.assert %matched, "exact literal must match"
    return %packed : i24
  }

  func.func @ordinary_literal() -> i24 {
    %text = obelisk_sim.string.literal "ABC"
    %packed = obelisk_sim.string.to_packed %text :
        (!obelisk_sim.string) -> i24
    return %packed : i24
  }
}

// CHECK-LABEL: func.func @exact_literal
// CHECK: %[[ABC:.*]] = arith.constant 4276803 : i24
// CHECK: return %[[ABC]] : i24
// CHECK-LABEL: func.func @ordinary_literal
// CHECK: obelisk_sim.string.to_packed
