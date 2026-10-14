// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=type-options | FileCheck %s --check-prefix=QUERY
// RUN: %t --coverage-output=%t.conflict.obcov --coverage-test=type-options-conflict +conflict | FileCheck %s --check-prefix=QUERY
// RUN: %t --coverage-output=%t.default.obcov --coverage-test=type-options-default +defaults | FileCheck %s --check-prefix=DEFAULTS
// RUN: not obelisk-cov report --format=text %t.obcov %t.conflict.obcov 2>&1 | FileCheck %s --check-prefix=CONFLICT
// RUN: not obelisk-cov report --format=text %t.obcov %t.default.obcov 2>&1 | FileCheck %s --check-prefix=CONFLICT
// RUN: obelisk-cov report --format=json --test=type-options %t.obcov %t.default.obcov | FileCheck %s --check-prefix=OVERRIDE
// RUN: obelisk-cov report --format=json --test=type-options-default %t.obcov %t.default.obcov | FileCheck %s --check-prefix=DEFAULT-JSON

// IEEE 1800-2023 19.7.1 permits these type options to be changed at any
// time. The pre-construction writes must affect configurations resolved by
// new(), and the later merge write must affect live queries at once. A type
// goal is recorded as a target and does not rescale coverage (19.11).
module top;
  int value;
  covergroup cg;
    cp: coverpoint value { bins zero = {0}; bins one = {1}; }
  endgroup
  cg a, b;
  int covered, total;
  real pct;
  initial begin
    if (!$test$plusargs("defaults")) begin
      cg::type_option.weight = 7;
      cg::type_option.comment = "group type";
      cg::cp::type_option.weight = 5;
      cg::cp::type_option.comment = "point type";
      $display("stored-initial %0d %0d %s|%s", cg::type_option.weight,
               cg::cp::type_option.weight, cg::type_option.comment,
               cg::cp::type_option.comment);
    end
    a = new;
    b = new;
    value = 0;
    a.sample();
    value = 1;
    b.sample();
    pct = cg::get_coverage(covered, total);
    $display("group-average %.0f %0d/%0d", pct, covered, total);
    pct = cg::cp::get_coverage(covered, total);
    $display("point-before %.0f %0d/%0d", pct, covered, total);
    if (!$test$plusargs("defaults"))
      cg::cp::type_option.goal = 50;
    pct = cg::cp::get_coverage(covered, total);
    $display("point-after %.0f %0d/%0d", pct, covered, total);
    if (!$test$plusargs("defaults")) begin
      if ($test$plusargs("conflict"))
        cg::type_option.goal = 90;
      else
        cg::type_option.goal = 80;
      cg::type_option.merge_instances = 1;
      $display("stored-final %0d %0d", cg::cp::type_option.goal,
               cg::type_option.merge_instances);
    end
    pct = cg::get_coverage(covered, total);
    $display("group-merged %.0f %0d/%0d", pct, covered, total);
    $finish;
  end
endmodule

// QUERY: stored-initial 7 5 group type|point type
// QUERY: group-average 50 2/4
// QUERY: point-before 50 2/4
// QUERY: point-after 50 2/4
// QUERY: stored-final 50 1
// QUERY: group-merged 100 2/2

// A run that never writes a type option keeps the defaults.
// DEFAULTS: group-average 50 2/4
// DEFAULTS: point-before 50 2/4
// DEFAULTS: point-after 50 2/4
// DEFAULTS: group-merged 50 2/4

// Merging runs that disagree on the type goal, whether both wrote it or one
// kept the default, is a conflict.
// CONFLICT: obelisk-cov: conflicting type goal for functional type

// Selecting only the run that wrote the options keeps its values; selecting
// only the run that wrote none reports the defaults.
// OVERRIDE: "functional_types":[{{[{]}}"id":{{[0-9]+}},"scope":{{[0-9]+}},"name":"cg",{{[^{]*}}"goal":80,"weight":7,"merge_instances":true,
// DEFAULT-JSON: "functional_types":[{{[{]}}"id":{{[0-9]+}},"scope":{{[0-9]+}},"name":"cg",{{[^{]*}}"goal":100,"weight":1,"merge_instances":false,
