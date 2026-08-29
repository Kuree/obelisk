// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2017 Clause 17 gives this retained checker executable behavior.
// Until its roadmap chunk lands, the source pipeline must reject it rather
// than silently producing an incomplete simulator.

checker activity_checker(input logic activity);
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
    // tranche; only the independent checker keeps this negative test invalid.
    $setup(activity, posedge clock &&& activity, 1);
  endspecify
endmodule

// CHECK: error: IEEE 1800-2017 Clause 17 checker instances are retained in semantic IR but are not executable yet
