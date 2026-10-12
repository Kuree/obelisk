// RUN: obelisk-opt %s --verify-diagnostics

func.func @numeric(%a: i64) -> i1 {
  // expected-error@+1 {{operand #0 must be}}
  %r = "simulation.semaphore.equal"(%a, %a) : (i64, i64) -> i1
  return %r : i1
}
