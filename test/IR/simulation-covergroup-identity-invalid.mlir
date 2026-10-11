// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  simulation.covergroup.decl @cg schema 1
  func.func @missing_left(%left: !simulation.covergroup_handle<@missing>,
                          %right: !simulation.covergroup_handle<@cg>) -> i1 {
    // expected-error @+1 {{references an unknown covergroup declaration}}
    %equal = simulation.covergroup.equal %left, %right :
      !simulation.covergroup_handle<@missing>, !simulation.covergroup_handle<@cg>
    return %equal : i1
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @missing_right(%left: !simulation.covergroup_handle<@cg>,
                           %right: !simulation.covergroup_handle<@missing>) -> i1 {
    // expected-error @+1 {{references an unknown covergroup declaration}}
    %equal = simulation.covergroup.equal %left, %right :
      !simulation.covergroup_handle<@cg>, !simulation.covergroup_handle<@missing>
    return %equal : i1
  }
}
