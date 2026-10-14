// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=type-options | FileCheck %s --check-prefix=QUERY
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov report --format=html %t.obcov | FileCheck %s --check-prefix=HTML

// IEEE 1800-2023 19.7.1 permits these type options to be changed at any
// time. The pre-construction writes must affect configurations resolved by
// new(), and the later goal / merge writes must affect live queries at once.
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
// QUERY: point-after 100 1/1
// QUERY: stored-final 50 1
// QUERY: group-merged 100 2/2

// REPORT: type cg ({{[0-9]+}}): 100.00%
// REPORT: type options: goal 80, weight 7, merge_instances true
// REPORT: type comment: "group type"
// REPORT: type comment: "point type"

// The resolved type options of the first instance are the seven written
// values: group goal, weight, merge_instances and comment, then the point's
// goal, weight and comment. The point item carries its type goal, weight and
// comment.
// JSON: "functional_types":[{{[{]}}"id":{{[0-9]+}},"scope":{{[0-9]+}},"name":"cg","base":0,"comment":"group type",{{[^{]*}}"goal":80,"weight":7,"merge_instances":true,
// JSON: "resolved_instance_options":[
// JSON-SAME: {"run":"{{[^"]*}}","instance":0,"owner":{{[0-9]+}},"owner_kind":1,"option":1,"value_kind":1,"value":80,"string_value":""},
// JSON-SAME: {"run":"{{[^"]*}}","instance":0,"owner":{{[0-9]+}},"owner_kind":1,"option":2,"value_kind":1,"value":7,"string_value":""},
// JSON-SAME: {"run":"{{[^"]*}}","instance":0,"owner":{{[0-9]+}},"owner_kind":1,"option":8,"value_kind":1,"value":1,"string_value":""},
// JSON-SAME: {"run":"{{[^"]*}}","instance":0,"owner":{{[0-9]+}},"owner_kind":1,"option":14,"value_kind":4,"value":0,"string_value":"group type"},
// JSON-SAME: {"run":"{{[^"]*}}","instance":0,"owner":{{[0-9]+}},"owner_kind":2,"option":1,"value_kind":1,"value":50,"string_value":""},
// JSON-SAME: {"run":"{{[^"]*}}","instance":0,"owner":{{[0-9]+}},"owner_kind":2,"option":2,"value_kind":1,"value":5,"string_value":""},
// JSON-SAME: {"run":"{{[^"]*}}","instance":0,"owner":{{[0-9]+}},"owner_kind":2,"option":14,"value_kind":4,"value":0,"string_value":"point type"},
// JSON-SAME: {"run":"{{[^"]*}}","instance":1,
// JSON: "type_goal":50,"type_weight":5,{{[^{]*}}"type_comment":"point type",

// The HTML report embeds the same type fields, with numbers as strings.
// HTML: "functional_types":[{{[{]}}"id":"{{[0-9]+}}","scope":"{{[0-9]+}}","name":"cg","base":"0","comment":"group type",{{[^{]*}}"goal":"80","weight":"7","merge_instances":true,
