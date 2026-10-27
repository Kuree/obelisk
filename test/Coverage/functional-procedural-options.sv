// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=procedural-options | FileCheck %s --check-prefix=QUERY
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

module top;
  int value;
  covergroup cg;
    cp0: coverpoint value { bins zero = {0}; bins one = {1}; }
    cp1: coverpoint value { bins zero = {0}; bins one = {1}; }
    cx: cross cp0, cp1;
  endgroup
  cg c;
  int covered, total;
  real pct;
  initial begin
    c = new;
    c.option.name = "mutated";
    c.option.comment = "group";
    c.option.weight = 2;
    c.cp0.option.comment = "point";
    c.sample();
    pct = c.cp0.get_inst_coverage(covered, total);
    $display("initial %.0f %0d/%0d", pct, covered, total);
    c.cp0.option.at_least = 2;
    pct = c.cp0.get_inst_coverage(covered, total);
    $display("raised %.0f %0d/%0d", pct, covered, total);
    c.cp0.option.weight = 3;
    c.cp0.option.goal = 50;
    c.sample();
    pct = c.cp0.get_inst_coverage(covered, total);
    $display("point %.0f %0d/%0d", pct, covered, total);
    c.option.at_least = 3;
    c.cx.option.weight = 0;
    c.cx.option.goal = 25;
    c.cx.option.comment = "cross";
    c.cx.option.at_least = 3;
    pct = c.cx.get_inst_coverage(covered, total);
    $display("cross-before %.0f %0d/%0d", pct, covered, total);
    c.sample();
    pct = c.cx.get_inst_coverage(covered, total);
    $display("cross-after %.0f %0d/%0d", pct, covered, total);
    pct = c.cp1.get_inst_coverage(covered, total);
    $display("second-point %.0f %0d/%0d", pct, covered, total);
    pct = c.cp1.get_coverage(covered, total);
    $display("second-point-type %.0f %0d/%0d", pct, covered, total);
    c.option.goal = 80;
    pct = c.get_inst_coverage(covered, total);
    $display("group %.1f %0d/%0d", pct, covered, total);
    $finish;
  end
endmodule

// QUERY: initial 50 1/2
// QUERY: raised 0 0/2
// QUERY: point 50 1/2
// QUERY: cross-before 0 0/4
// QUERY: cross-after 25 1/4
// QUERY: second-point 50 1/2
// QUERY: second-point-type 50 1/2
// QUERY: group 50.0 3/8

// REPORT: functional: 50.00% (3/8)
// REPORT: instance mutated: 50.00% (3/8)
// REPORT: comment: "group"
// REPORT-DAG: coverpoint cp0: 1/2 (50.00%)
// REPORT-DAG: comment: "point"
// REPORT-DAG: bin zero: 3 [covered] {at_least 2}
// REPORT-DAG: coverpoint cp1: 1/2 (50.00%)
// REPORT-DAG: bin zero: 3 [covered] {at_least 3}
// REPORT-DAG: cross cx: 1/4 (25.00%)
// REPORT-DAG: comment: "cross"
// REPORT-DAG: automatic: 1/4

// IEEE 1800-2023 19.7.1: procedural option writes are recorded per instance.
// Goals are recorded as targets and do not rescale coverage (19.11).
// The group keeps the written name, comment, goal and weight; the point keeps
// its comment, goal and weight with at_least raised on every bin; the cross
// keeps its comment, goal, weight and the at_least applied to its automatic
// bins. The resolved options record the cross weight of zero.
// JSON: "resolved_instance_options":[
// JSON-DAG: "owner_kind":1,"option":5,"value_kind":1,"value":0,
// JSON: "functional_instance_groups":[{{[{]}}"type":{{[0-9]+}},"name":"mutated",{{[^{]*}}"comment":"group","goal":80,"weight":2,{{[^{]*}}"cross_num_print_missing":0,
// JSON-DAG: "name":"cp0","comment":"point",{{[^{]*}}"goal":50,"weight":3,{{.*}}"hierarchy":"top.cg.cp0.zero",{{[^}]*}}"at_least":2,{{.*}}"hierarchy":"top.cg.cp0.one",{{[^}]*}}"at_least":2,
// JSON-DAG: "name":"cx","comment":"cross",{{[^{]*}}"goal":25,"weight":0,{{[^{]*}}"automatic_at_least":3,
