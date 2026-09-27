// RUN: obelisk-opt %s --split-input-file --verify-diagnostics
module {
  func.func @bad_edges(%handle: i64) {
    // expected-error@+1 {{edge inventory exceeds the watched operand prefix}}
    "schedule.suspend.any"(%handle)[^resume] {edges = array<i32: 0, 1>} : (i64) -> ()
  ^resume:
    return
  }
}
// -----
module {
  func.func @bad_join(%handle: i64) {
    // expected-error@+1 {{process count exceeds the operand inventory}}
    "schedule.suspend.join"(%handle)[^resume] {process_count = 2 : i64, kind = 0 : i32} : (i64) -> ()
  ^resume:
    return
  }
}
// -----
module {
  func.func @bad_observer(%handle: i64) {
    // expected-error@+1 {{requires typed observer identity, result, and dependency metadata}}
    %0 = "schedule.observer"(%handle) {capture_count = 1 : i64} : (i64) -> i64
    return
  }
}
