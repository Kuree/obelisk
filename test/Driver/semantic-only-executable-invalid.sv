// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2017 Clauses 17, 30, and 31 give these retained constructs
// executable behavior. Until their roadmap chunks land, the source pipeline
// must reject them rather than silently producing an incomplete simulator.

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
    (activity *> observed) = 1;
    $setup(activity, posedge clock, 1);
  endspecify
endmodule

// CHECK: error: IEEE 1800-2017 Clause 17 checker instances are retained in semantic IR but are not executable yet
// CHECK: error: IEEE 1800-2017 Clause 30 specify pulse controls are retained in semantic IR but are not executable yet
// CHECK: error: IEEE 1800-2017 Clause 30 specify timing paths are not executable yet for this form
// CHECK: error: IEEE 1800-2017 Clause 31 system timing checks are retained in semantic IR but are not executable yet
