// RUN: obelisk-opt %s --split-input-file --verify-diagnostics
module {
  func.func @bad_edges(%handle: i64) {
    // expected-error@+1 {{edge inventory exceeds the watched operand prefix}}
    schedule.suspend.any %handle edges [0, 1] to ^resume : i64
  ^resume:
    return
  }
}
// -----
module {
  func.func @bad_join(%handle: i64) {
    // expected-error@+1 {{process count exceeds the operand inventory}}
    schedule.suspend.join all %handle processes 2 to ^resume : i64
  ^resume:
    return
  }
}
// -----
module {
  func.func @bad_observer(%handle: i64) {
    // expected-error@+1 {{requires typed observer identity, result, and dependency metadata}}
    %0 = schedule.observer(%handle : i64) captures 1
    return
  }
}
