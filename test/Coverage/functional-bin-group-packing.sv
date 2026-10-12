// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=OUT
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=BINS
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 1800-2023 19.5.1 and 19.5.3. Late-generated groups must retain
// declaration order, empty groups, shared default domains, signed partition
// boundaries, four-state sampling, and exclusion from the cross denominator.
module top;
  bit [2:0] value;
  bit signed [2:0] signed_value;
  logic [2:0] logic_value;
  covergroup cg(input int maximum);
    option.auto_bin_max = maximum;
    zero: coverpoint value {
      option.auto_bin_max = 0;
      ignore_bins ignored = {0};
    }
    first: coverpoint value;
    defaults: coverpoint value {
      bins other[] = default;
      bins duplicate[] = default;
      bins none[] = {7} with (item == 0);
      bins one = {1};
    }
    empty_default: coverpoint value {
      bins other[] = default;
      bins all_values = {[0:7]};
    }
    last: coverpoint signed_value;
    logic_cp: coverpoint logic_value { ignore_bins ignored = {3}; }
    product: cross first, last {
      bins selected = binsof(first) intersect {[0:1]};
    }
  endgroup
  cg cov;
  initial begin
    cov = new(3);
    signed_value = -4;
    logic_value = 'x;
    cov.sample();
    value = 7;
    signed_value = 3;
    logic_value = 7;
    cov.sample();
    $display("FIRST %0.2f LAST %0.2f", cov.first.get_inst_coverage(),
             cov.last.get_inst_coverage());
    $finish;
  end
endmodule

// OUT: FIRST 66.67 LAST 66.67
// REPORT: functional: 49.21% (8/18)
// REPORT-DAG: coverpoint defaults: 0/1 (0.00%)
// REPORT-DAG: bin other[0]: 1 [excluded] {default}
// REPORT-DAG: bin other[7]: 1 [excluded] {default}
// REPORT-DAG: bin duplicate[0]: 1 [excluded] {default}
// REPORT-DAG: bin duplicate[7]: 1 [excluded] {default}
// REPORT-DAG: coverpoint empty_default: 1/1 (100.00%)
// REPORT-DAG: coverpoint zero: 0/0 (0.00%)
// REPORT-DAG: bin auto[-4:-3]: 1 [covered] {automatic}
// REPORT-DAG: bin auto[0:3]: 1 [covered] {automatic}
// REPORT-DAG: coverpoint logic_cp: 1/3 (33.33%)
// REPORT-DAG: cross product: 2/7 (28.57%)

// One configuration resolves every item's bins in declaration order with
// dense ordinals: default bin arrays carry the default flag, automatic ranges
// the automatic flag, and ignored values remain in place (19.5.1-19.5.4).
// BINS: "resolved_functional_bins":[
// BINS-SAME: {{[{][^}]*}}"item":[[ZERO:[0-9]+]],"name":"ignored",{{[^}]*}}"flags":4,"ordinal":0,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[FIRST:[0-9]+]],"name":"auto[0:1]",{{[^}]*}}"flags":64,"ordinal":0,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[FIRST]],"name":"auto[2:3]",{{[^}]*}}"flags":64,"ordinal":1,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[FIRST]],"name":"auto[4:7]",{{[^}]*}}"flags":64,"ordinal":2,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS:[0-9]+]],"name":"other[0]",{{[^}]*}}"flags":1,"ordinal":0,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"other[2]",{{[^}]*}}"flags":1,"ordinal":1,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"other[3]",{{[^}]*}}"flags":1,"ordinal":2,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"other[4]",{{[^}]*}}"flags":1,"ordinal":3,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"other[5]",{{[^}]*}}"flags":1,"ordinal":4,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"other[6]",{{[^}]*}}"flags":1,"ordinal":5,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"other[7]",{{[^}]*}}"flags":1,"ordinal":6,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"duplicate[0]",{{[^}]*}}"flags":1,"ordinal":7,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"duplicate[2]",{{[^}]*}}"flags":1,"ordinal":8,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"duplicate[3]",{{[^}]*}}"flags":1,"ordinal":9,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"duplicate[4]",{{[^}]*}}"flags":1,"ordinal":10,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"duplicate[5]",{{[^}]*}}"flags":1,"ordinal":11,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"duplicate[6]",{{[^}]*}}"flags":1,"ordinal":12,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"duplicate[7]",{{[^}]*}}"flags":1,"ordinal":13,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[DEFAULTS]],"name":"one",{{[^}]*}}"flags":0,"ordinal":14,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[EMPTY_DEFAULT:[0-9]+]],"name":"all_values",{{[^}]*}}"flags":0,"ordinal":0,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[LAST:[0-9]+]],"name":"auto[-4:-3]",{{[^}]*}}"flags":64,"ordinal":0,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[LAST]],"name":"auto[-2:-1]",{{[^}]*}}"flags":64,"ordinal":1,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[LAST]],"name":"auto[0:3]",{{[^}]*}}"flags":64,"ordinal":2,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[LOGIC_CP:[0-9]+]],"name":"ignored",{{[^}]*}}"flags":4,"ordinal":0,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[LOGIC_CP]],"name":"auto[0:1]",{{[^}]*}}"flags":64,"ordinal":1,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[LOGIC_CP]],"name":"auto[2:3]",{{[^}]*}}"flags":64,"ordinal":2,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[LOGIC_CP]],"name":"auto[4:7]",{{[^}]*}}"flags":64,"ordinal":3,
// BINS-SAME: {{[^}]*}}},{{[{][^}]*}}"item":[[PRODUCT:[0-9]+]],"name":"selected",{{[^}]*}}"flags":0,"ordinal":0,
// BINS-SAME: {{[^}]*}}}],
// INSPECT: functional-configurations: 1
