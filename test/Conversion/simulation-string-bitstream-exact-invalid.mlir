// RUN: obelisk-opt %s --verify-diagnostics

module {
  func.func @invalid_exact_width(%text: !simulation.string) {
    // expected-error@+1 {{packed result width must be a nonzero multiple of eight}}
    %packed, %matched = simulation.string.to_packed_exact %text :
        (!simulation.string) -> (i12, i1)
    return
  }
}
