// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  func.func @f(%ctx: !simulation.context) {
    // expected-error@+1 {{references an unknown covergroup declaration}}
    %token = simulation.covergroup.prepare %ctx from @missing payloads []
      expression_ids [] : () -> i64
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 10
  func.func @f(%ctx: !simulation.context, %value: i1) {
    // expected-error@+1 {{requires unique positive FunctionalExpression IDs}}
    %token = simulation.covergroup.prepare %ctx from @cg
      payloads [%value, %value] expression_ids [100, 100]
      : (i1, i1) -> i64
    return
  }
}

// -----

module {
  func.func @f(%ctx: !simulation.context, %token: i64, %queue: i64) {
    // expected-error@+1 {{requires an unbounded CrossQueueType}}
    simulation.covergroup.candidates %ctx, %token into %queue
      selector 70 maximum 8 : i64
    return
  }
}

// -----

!queue = !simulation.queue<!simulation.unpacked_struct<[#simulation.field<name = "a", type = i1, ordinal = 0, packedOffset = 0>]>, 0>
module {
  func.func @f(%ctx: !simulation.context, %token: i64, %queue: !queue) {
    // expected-error@+1 {{requires a positive selector and maximum in [0, 4096]}}
    simulation.covergroup.candidates %ctx, %token into %queue
      selector 70 maximum 4097 : !queue
    return
  }
}

// -----

module {
  func.func @f(%ctx: !simulation.context, %token: i64, %queue: i64) {
    // expected-error@+1 {{requires an unbounded CrossQueueType}}
    simulation.covergroup.bind_with %ctx, %token from %queue selector 70 : i64
    return
  }
}
