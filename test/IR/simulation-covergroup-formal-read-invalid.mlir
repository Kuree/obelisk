// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  simulation.covergroup.decl @cg schema 1
  func.func @missing_id(%ctx: !simulation.context, %cg: !simulation.covergroup_handle<@cg>) {
    // expected-error @+1 {{FunctionalFormal ID must be a positive signed 64-bit value}}
    %value = simulation.covergroup.formal_read %ctx, %cg[0] : !simulation.context, !simulation.covergroup_handle<@cg> -> !simulation.string
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @unsupported_result(%ctx: !simulation.context, %cg: !simulation.covergroup_handle<@cg>) {
    // expected-error @+1 {{requires an integral, real, or string formal value or argument reference}}
    %value = simulation.covergroup.formal_read %ctx, %cg[1] : !simulation.context, !simulation.covergroup_handle<@cg> -> !simulation.queue<i32, 0>
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @unsupported_reference(%ctx: !simulation.context, %cg: !simulation.covergroup_handle<@cg>) {
    // expected-error @+1 {{requires an integral, real, or string formal value or argument reference}}
    %value = simulation.covergroup.formal_read %ctx, %cg[1] : !simulation.context, !simulation.covergroup_handle<@cg> -> !simulation.argument_ref<!simulation.queue<i32, 0>>
    return
  }
}
