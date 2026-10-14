// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

// CHECK: module attributes {simulation.gc_polled, simulation.pulse_on_detect = true, simulation.pulse_reject_percent = 80 : i32}
module attributes {simulation.gc_polled, simulation.pulse_on_detect = true,
                   simulation.pulse_reject_percent = 80 : i32} {
  // CHECK-LABEL: func.func private @summarized
  // CHECK-SAME: simulation.integer_range = #simulation.integer_range<umin = 0 : i8, umax = 3 : i8, smin = 0 : i8, smax = 3 : i8>
  // CHECK-SAME: simulation.result_exprs = [#simulation.integer_expression<kind = add, width = 8, operands = [#simulation.integer_expression<kind = argument, width = 8, index = 0>, #simulation.integer_expression<kind = constant, width = 8, value = 1 : i8>]>, unit]
  func.func private @summarized(
      i8 {simulation.integer_range = #simulation.integer_range<
          umin = 0 : i8, umax = 3 : i8, smin = 0 : i8, smax = 3 : i8>})
      attributes {simulation.result_exprs = [
        #simulation.integer_expression<kind = add, width = 8, operands = [
          #simulation.integer_expression<kind = argument, width = 8, index = 0>,
          #simulation.integer_expression<kind = constant, width = 8,
                                         value = 1 : i8>]>,
        unit]}

  // CHECK-LABEL: func.func private @minimized
  // CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "z3", queries = 9, alternatives = 3 -> 2, literals = 6 -> 4>, #simulation.boolean_minimization<consequent, solver = "heuristic", queries = 0, alternatives = 2 -> 2, literals = 2 -> 2>]
  func.func private @minimized() attributes {simulation.boolean_minimizations = [
    #simulation.boolean_minimization<antecedent, solver = "z3", queries = 9,
                                     alternatives = 3 -> 2, literals = 6 -> 4>,
    #simulation.boolean_minimization<consequent, solver = "heuristic",
                                     queries = 0, alternatives = 2 -> 2,
                                     literals = 2 -> 2>]}
}
