// RUN: obelisk-opt %s --verify-diagnostics

module {
  func.func @invalid_exact_width(%text: !obelisk_sim.string) {
    // expected-error@+1 {{packed result width must be a nonzero multiple of eight}}
    %packed, %matched = obelisk_sim.string.to_packed_exact %text :
        (!obelisk_sim.string) -> (i12, i1)
    return
  }
}
