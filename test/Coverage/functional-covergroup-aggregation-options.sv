// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=group-aggregation | FileCheck %s --check-prefix=QUERY
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

module top;
  bit [1:0] value;

  covergroup cg(input int instance_weight, input int instance_goal);
    option.weight = instance_weight;
    option.goal = instance_goal;
    cp: coverpoint value {
      bins zero = {0};
      bins one = {1};
      bins two = {2};
      bins three = {3};
    }
  endgroup

  cg high_weight;
  cg low_weight;
  cg zero_weight;
  initial begin
    high_weight = new(3, 50);
    low_weight = new(1, 100);
    zero_weight = new(0, 100);
    value = 0;
    high_weight.sample();
    $display("high %.6f", high_weight.get_inst_coverage());
    $display("low %.6f", low_weight.get_inst_coverage());
    $display("zero %.6f", zero_weight.get_inst_coverage());
    $display("type %.6f", cg::get_coverage());
    $display("global %.6f", $get_coverage());
    $finish;
  end
endmodule

// The point remains at its own default goal of 100 (25%). The strong group
// normalizes that result to its group goal of 50 (50%). Type coverage then
// weights the three instances as (3*50 + 1*0 + 0*0) / 4.
// QUERY: high 50.000000
// QUERY-NEXT: low 0.000000
// QUERY-NEXT: zero 0.000000
// QUERY-NEXT: type 37.500000
// QUERY-NEXT: global 37.500000
// REPORT: functional: 37.50% (1/12)
// REPORT-DAG: type cg ({{[0-9]+}}): 37.50%
// REPORT-DAG: coverpoint cp: 1/4 (25.00%)
// REPORT-DAG: instance $auto$1: 50.00% (1/4)
// JSON: "goal":50,"weight":3
// JSON: "goal":100,"weight":1
// JSON: "goal":100,"weight":0
// JSON: "name":"cp","comment":"","hierarchy":"top.cg.cp","kind":1,"ordinal":0,"goal":100,"weight":1
