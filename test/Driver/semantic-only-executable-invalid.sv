// RUN: not talos -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2023 17.7.2 randomizes a free checker variable under the
// checker's assumptions. Until that lands, the source pipeline must reject it
// rather than silently producing an incomplete simulator.

checker activity_checker(input logic activity);
  rand bit free;
endchecker

module semantic_only_executable_invalid(
    input wire activity,
    input wire clock,
    output wire observed);
  assign observed = activity;
  activity_checker checker_i(activity);
  specify
    pulsestyle_onevent observed;
    if (activity) (activity *> observed) = 1;
    // Clause 31.7 bare direct conditions are executable in the current G5
    // tranche; only the checker's free variable keeps this test invalid.
    $setup(activity, posedge clock &&& activity, 1);
  endspecify
endmodule

// CHECK: error: IEEE 1800-2023 17.7 free checker variables are not executable yet
