// RUN: talos-opt %s --verify-diagnostics

func.func @different_specializations(%a: !simulation.mailbox<i32, 1>,
                                    %b: !simulation.mailbox<i32, 2>) -> i1 {
  // expected-error@+1 {{same type}}
  %r = "simulation.mailbox.equal"(%a, %b) : (!simulation.mailbox<i32, 1>, !simulation.mailbox<i32, 2>) -> i1
  return %r : i1
}
